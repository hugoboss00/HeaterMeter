//#include "client_http.hpp"
#include "server_http.hpp"
//#include "utility.hpp"

#include <time.h>
#include <pthread.h>

// Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

// Added for the default_resource example
#include <algorithm>
#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>
#ifdef HAVE_OPENSSL
#include "crypto.hpp"
#endif

using namespace std;
// Added for the json-example:
using namespace boost::property_tree;

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
//using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;

extern void getProbeData(ptree &pt);
extern void getConfigData(ptree &pt);
extern void getHistory(stringstream &csv, int count);
extern void handleCommandUrl(const char *URL);

void *do_server(void *);
static pthread_t t_server;


template<typename ... Args>
string string_format( const std::string& format, Args ... args )
{
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    unique_ptr<char[]> buf( new char[ size ] ); 
    snprintf( buf.get(), size, format.c_str(), args ... );
    return string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}
#if 0
  "time":1405429467,
  "set":65,
  "lid":38,
  "fan":{"c":0,"a":13,"f":10},
  "adc":[0,0,0,0,0,3],
  "temps":[
    {
      "n":"Probe 0",
      "c":78.6,
      "dph":1.3,
      "rf":{"s":1,"b":0},
      "a":{"l":-40,"h":200,"r":null}
    },
    ...
  ]
#endif



void start_server()
{
	pthread_create(&t_server, NULL, do_server, NULL);
	pthread_setname_np(t_server, "gom_srv");
}


void *do_server(void *) {
  // HTTP-server at port 8080 using 1 thread
  // Unless you do more heavy non-threaded processing in the resources,
  // 1 thread is usually faster than several threads
  HttpServer server;
  server.config.port = 8088;

  // Add resources using path-regex and method-string, and an anonymous function
  // POST-example for the path /string, responds the posted string
  server.resource["^/string$"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    // Retrieve string:
    auto content = request->content.string();
    // request->content.string() is a convenience function for:
    // stringstream ss;
    // ss << request->content.rdbuf();
    // auto content=ss.str();

    *response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n"
              << content;


    // Alternatively, use one of the convenience functions, for instance:
    // response->write(content);
  };

  server.resource["^/json$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
	  printf("path: %s\n", request->path.c_str());
	  printf("%s\n", request->content.string().c_str());
      //read_json(request->content, pt);

      getProbeData(pt);
		
		
		std::stringstream ss;
		boost::property_tree::json_parser::write_json(ss, pt,false);
			
		//printf("string:%s\n", ss.str().c_str());

      *response << "HTTP/1.1 200 OK\r\n"
                << "Content-Length: " << ss.str().length() << "\r\n\r\n"
                << ss.str();
    }
    catch(const exception &e) {
      *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
                << e.what();
    }


  };

  server.resource["^/conf$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
		ptree pt;

		getConfigData(pt);


		std::stringstream ss;
		boost::property_tree::json_parser::write_json(ss, pt,false);
			
		printf("conf:%s\n", ss.str().c_str());

		*response << "HTTP/1.1 200 OK\r\n"
				<< "Content-Length: " << ss.str().length() << "\r\n\r\n"
				<< ss.str();
    }
    catch(const exception &e) {
      *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
                << e.what();
    }


  };


  // GET-example for the path /info
  // Responds with request-information
  server.resource["^/hist$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	SimpleWeb::CaseInsensitiveMultimap querymap = request->parse_query_string();
    stringstream stream;
	stringstream csv;
	int download = 0;
	int count = -1;

	SimpleWeb::CaseInsensitiveMultimap::iterator it = querymap.find("dl");
    if (it != querymap.end())
	{
		download = 1;
	}
	it = querymap.find("nancnt");
    if (it != querymap.end())
	{
		stringstream conv(it->second);
		int value = 0;
		conv >> value;
		
		switch (value)
		{
		case 460:
			count = 1;
			break;
		case 360:
			count = 6;
			break;
		case 240:
			count = 12;
			break;
		default:
			count = 24;
			break;
		}
	}
	
	
	getHistory(csv, count);
	if (download)
	{		
		time_t rawtime;
		struct tm * timeinfo;
		char buffer[80];

		time (&rawtime);
		timeinfo = localtime(&rawtime);

		strftime(buffer,sizeof(buffer),"gom_%Y-%m-%d_%H_%M_%S_history.txt",timeinfo);
		std::string filename(buffer);
		stream << "HTTP/1.1 200 OK\r\n"
					<< "Content-Disposition: attachment; filename=" << filename << "\r\n"
					<< "Content-Length: " << csv.str().length() << "\r\n\r\n"
					<< csv.str();
	}
	else	
	{
		stream << "HTTP/1.1 200 OK\r\n"
					<< "Content-Type: text/plain\r\n"
					<< "Content-Length: " << csv.str().length() << "\r\n\r\n"
					<< csv.str();
	}
    *response << stream.str();
  };

  // GET-example for the path /match/[number], responds with the matched string in path (number)
  // For instance a request GET /match/123 will receive: 123
  server.resource["^/set$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	SimpleWeb::CaseInsensitiveMultimap querymap = request->parse_query_string();
	for (SimpleWeb::CaseInsensitiveMultimap::iterator it = querymap.begin(); it != querymap.end(); it++)
	{
		string cmd = "set?" + it->first + "=" + it->second;
		printf("q: %s=%s\n", it->first.c_str(), it->second.c_str());
		handleCommandUrl(SimpleWeb::Percent::decode(cmd).c_str());
	}
	response->write("");
  };


  // GET-example for the path /match/[number], responds with the matched string in path (number)
  // For instance a request GET /match/123 will receive: 123
  server.resource["^/match/([0-9]+)$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	printf("path: %s\n", request->path.c_str());
    printf("param0: %s\n", request->path_match[0].str().c_str());
    printf("param1: %s\n", request->path_match[1].str().c_str());
    printf("param2: %s\n", request->path_match[2].str().c_str());
    response->write(request->path_match[1]);
  };

  // GET-example simulating heavy work in a separate thread
  server.resource["^/stream$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    thread work_thread([response] {
		int running = 1;
		*response << "HTTP/1.1 200 OK\r\n"
		<< "Content-Type: text/event-stream\r\n"
		<< "Cache-Control: no-cache\r\n"
		<< "Connection: keep-alive\r\n\r\n";
		response->send();
		while (running)
		{
			ptree pt;

			getProbeData(pt);
			std::stringstream ss;
			ss << "event: hmstatus\n";
			ss << "data: ";
			boost::property_tree::json_parser::write_json(ss, pt, false);
			ss << "\n\n";
			
			//printf("string:%s\n", ss.str().c_str());

			*response << ss.str();
			response->send([&running](const SimpleWeb::error_code &ec) {
                  if(ec)
				  {
					running = 0;
				  }
                });



			this_thread::sleep_for(chrono::seconds(10));

	  }
    });
    work_thread.detach();
  };

  // Default GET-example. If no other matches, this anonymous function will be called.
  // Will respond with content in the web/-directory, and its subdirectories.
  // Default file: index.html
  // Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
  server.default_resource["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      auto web_root_path = boost::filesystem::canonical("websrv/web");
      auto path = boost::filesystem::canonical(web_root_path / request->path);
      // Check if path is within web_root_path
      if(distance(web_root_path.begin(), web_root_path.end()) > distance(path.begin(), path.end()) ||
         !equal(web_root_path.begin(), web_root_path.end(), path.begin()))
        throw invalid_argument("path must be within root path");
      if(boost::filesystem::is_directory(path))
        path /= "index.html";

      SimpleWeb::CaseInsensitiveMultimap header;

      // Uncomment the following line to enable Cache-Control
      // header.emplace("Cache-Control", "max-age=86400");

#ifdef HAVE_OPENSSL
//    Uncomment the following lines to enable ETag
//    {
//      ifstream ifs(path.string(), ifstream::in | ios::binary);
//      if(ifs) {
//        auto hash = SimpleWeb::Crypto::to_hex_string(SimpleWeb::Crypto::md5(ifs));
//        header.emplace("ETag", "\"" + hash + "\"");
//        auto it = request->header.find("If-None-Match");
//        if(it != request->header.end()) {
//          if(!it->second.empty() && it->second.compare(1, hash.size(), hash) == 0) {
//            response->write(SimpleWeb::StatusCode::redirection_not_modified, header);
//            return;
//          }
//        }
//      }
//      else
//        throw invalid_argument("could not read file");
//    }
#endif

      auto ifs = make_shared<ifstream>();
      ifs->open(path.string(), ifstream::in | ios::binary | ios::ate);

      if(*ifs) {
        auto length = ifs->tellg();
        ifs->seekg(0, ios::beg);

        header.emplace("Content-Length", to_string(length));
        response->write(header);

        // Trick to define a recursive function within this scope (for example purposes)
        class FileServer {
        public:
          static void read_and_send(const shared_ptr<HttpServer::Response> &response, const shared_ptr<ifstream> &ifs) {
            // Read and send 128 KB at a time
            static vector<char> buffer(131072); // Safe when server is running on one thread
            streamsize read_length;
            if((read_length = ifs->read(&buffer[0], static_cast<streamsize>(buffer.size())).gcount()) > 0) {
              response->write(&buffer[0], read_length);
              if(read_length == static_cast<streamsize>(buffer.size())) {
                response->send([response, ifs](const SimpleWeb::error_code &ec) {
                  if(!ec)
                    read_and_send(response, ifs);
                  else
                    cerr << "Connection interrupted" << endl;
                });
              }
            }
          }
        };
        FileServer::read_and_send(response, ifs);
      }
      else
        throw invalid_argument("could not read file");
    }
    catch(const exception &e) {
	  printf("Error to get path %s\n", request->path.c_str());
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
    }
  };

  server.on_error = [](shared_ptr<HttpServer::Request> /*request*/, const SimpleWeb::error_code & /*ec*/) {
    // Handle errors here
    // Note that connection timeouts will also call this handle with ec set to SimpleWeb::errc::operation_canceled
  };

  thread server_thread([&server]() {
    // Start server
    server.start();
  });
  server_thread.join();
  return 0;
}

void wait_server()
{
	pthread_join(t_server, NULL);
}
