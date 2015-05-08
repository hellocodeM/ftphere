#include <iostream>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <unistd.h>

#include <boost/asio.hpp>

#include "logger.hpp"
#include "cmd.hpp"
#include "util.hpp"
#include "ftp_server.hpp"

using namespace ftp;
using namespace boost::asio;

int StatusCode(const string& response) {
    return std::stoi(Split(response, ' ').front());
}

void StartServer() {
    static bool started = false;
    if (started == false) {
        started = true;
        std::thread server([=]() {
                FTPServer s(8080, 8081);
                s.Run();
                });
        server.detach();
        sleep(1);
    }
}

bool TestLogger() {
    const char* log_path = "./test.log";
    Logger logger(log_path);
    logger.Log("wow, amazing", Logger::INFO);
    logger.Log("oh, no");
    logger.Log("oh, shit", Logger::ERROR);
    puts("pass-test: logger");
    return true;
}

bool TestCMD() {
    assert(ResolveCMD("USER") == CMD::USER);
    assert(ResolveCMD("PASS") == CMD::PASS);
    assert(ResolveCMD("shit") == CMD::BAD);
    assert(ResolveCMD("user") == CMD::USER);
    assert(ResolveCMD("pass") == CMD::PASS);
    puts("pass-test: cmd");
    return true;
}

bool TestResponse() {
    assert(Response(200) == "Command okay.");
    assert(Response(214) == "Help message.");
    assert(Response(530) == "Not logged in.");
    puts("pass-test: response");
    return true;
}

bool TestSplit() {
    std::string str = "USER anonymous";
    std::vector<std::string> res = { "USER", "anonymous" };
    auto splited = Split(str, ' ');
    assert(res == splited);

    str = "shit";
    res = { "shit" };
    splited = Split(str, ' ');
    assert(res == splited);
    puts("pass-test: split");
    return true;
}

/**
 * Short request. 
 * Send a req sequence, and receive the response.
 */
template<typename Fn>
void Request(const string& req, Fn callback) {    
    /* create server */
    StartServer();

    /* create client */
    io_service ios;
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 8080);
    ip::tcp::socket socket(ios);
    socket.connect(ep);

    streambuf buff;
    std::istream is(&buff);
    std::string line;

    socket.write_some(buffer(req));
    read_until(socket, buff, "\r\n");
    std::getline(is, line, '\r');
    callback(line);
    
    socket.write_some(buffer("QUIT\r\n"));
}

/* Long request */
template<typename Fn>
void Request(Fn callback) {
    StartServer();

    io_service ios;
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 8080);
    ip::tcp::socket socket(ios);
    socket.connect(ep);
    
    callback(socket);
    socket.write_some(buffer("QUIT\r\n"));
}

template<typename Fn>
void Receive(ip::tcp::endpoint& ep, Fn fn) {
    StartServer();    
    
    std::thread anon([&]() {
        try {
            io_service ios;
            ip::tcp::acceptor acceptor(ios, ep);
            ip::tcp::socket socket(ios);
            acceptor.accept(socket);
            fn(socket);
            acceptor.close();
        } catch (std::exception& e) {
            printf("line %d: %s\n", __LINE__, e.what());
        }
    });
    anon.detach();
}

bool TestSignin() {
    Request("USER ming\r\n", [](auto& res) {
        assert(StatusCode(res) == 331);
    });
    
    Request("PASS shit\r\n", [](auto& res) {
        assert(StatusCode(res) == 230);
    });
    puts("pass-test: sign in");
    
    return true;
}

bool TestPWD() {
    Request("PWD\r\n", [](auto& res) {
        assert(StatusCode(res) == 257);
    });
    
    puts("pass-test: pwd");
    return true;
}

bool TestCWD() {
    Request("CWD /shit\r\n", [](auto& res) {
        assert(StatusCode(res) == 250);
    });

    puts("pass-test: cwd");
    return true;
}

bool TestPORT() {
    /* port 1025 */
    /* Nobody listening at 1025, so this request will fail. */
    Request("PORT 127,0,0,1,4,1\r\n", [](auto& res) {
        assert(StatusCode(res) == 425);
    });

    /* set a listener at 1025, then send request. */
    ip::tcp::endpoint ep(ip::tcp::v4(), 1025);
    Receive(ep, [](auto& socket) {
        assert(socket.remote_endpoint().address().to_string() == "127.0.0.1");
    });

    Request("PORT 127,0,0,1,4,1\r\n", [](auto& res) {
        assert(StatusCode(res) == 200);
    });
    puts("pass-test: port");
    return true;
}

bool TestFileInfo() {
    std::cout << make_file_info("/") << std::endl;
    puts("pass-test: file info");
    return true;
}

bool TestLIST() {
    ip::tcp::endpoint ep(ip::tcp::v4(), 1025);
    Receive(ep, [](auto& socket) {
        boost::system::error_code ec;
        while (!ec) {
            std::string res = ReadLine(socket, ec);
            std::cout << res << std::endl;
        }
    });

    Request([](auto& socket){
        WriteLine(socket, "PORT 127,0,0,1,4,1");
        assert(StatusCode(ReadLine(socket)) == 200);

        WriteLine(socket, "LIST /");
        
        assert(StatusCode(ReadLine(socket)) == 150);
        assert(StatusCode(ReadLine(socket)) == 226);
    });

    puts("pass-test: list");
    return true;
}

bool TestRETR() {
    ip::tcp::endpoint ep(ip::tcp::v4(), 1025);
    Receive(ep, [](auto& socket) {
        puts("content of test.cc");
        FILE* file = fopen("/tmp/ftp-test.dat", "w+");
        char buff[128];
        size_t len;
        boost::system::error_code ec;
        
        while (!ec) {
            len = socket.read_some(buffer(buff, 128), ec);
            fwrite(buff, 1, len, file);
            fwrite(buff, 1, len, stdout);
        }
        fclose(file);
    });

    Request([](auto& socket) {
        WriteLine(socket, "PORT 127,0,0,1,4,1");
        assert(StatusCode(ReadLine(socket)) == 200);

        WriteLine(socket, "RETR /test.cc");
        assert(StatusCode(ReadLine(socket)) == 150);
        assert(StatusCode(ReadLine(socket)) == 226);
    });
    puts("pass-tes: retr");
    return true;
}

int main()
{
    assert(TestLogger());
    assert(TestCMD());
    assert(TestResponse());
    assert(TestSplit());
    assert(TestSignin());
    assert(TestPWD());
    assert(TestCWD());
    assert(TestPORT());
    assert(TestFileInfo());
    assert(TestLIST());
    assert(TestRETR());
    puts("pass all test!");
    return 0;
}
