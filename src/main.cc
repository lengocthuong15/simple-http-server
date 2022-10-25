#include <sys/resource.h>
#include <sys/time.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "http_message.h"
#include "http_server.h"
#include "uri.h"
#include <storage.h>

using simple_http_server::HttpMethod;
using simple_http_server::HttpRequest;
using simple_http_server::HttpResponse;
using simple_http_server::HttpServer;
using simple_http_server::HttpStatusCode;

int main(void) {
  std::string host = "0.0.0.0";
  int port = 8080;
  HttpServer server(host, port);

  try {

    std::cout << "Starting the web server.." << std::endl;
    server.Start();
    std::cout << "Server listening on " << host << ":" << port << std::endl;

    std::cout << "Enter [q] to stop the server" << std::endl;
    std::string command;
    while (std::cin >> command, command != "q")
      ;
    std::cout << "'q' command entered. Stopping the web server.."
              << std::endl;
    server.Stop();
    std::cout << "Server stopped" << std::endl;
  } catch (std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
    return -1;
  }
  return 0;
}
