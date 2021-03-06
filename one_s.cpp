#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <set>
#include <cstdint>
#include <thread>
#include <vector>

#ifdef _WIN32
	#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
	#ifndef _WIN32_WINNT
	#endif
	#include <winsock2.h>
	#include <Ws2tcpip.h>
#else
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
#endif

#ifdef _CRT_SECURE_NO_WARNINGS
#undef _CRT_SECURE_NO_WARNINGS
#endif
#define _CRT_SECURE_NO_WARNINGS 1

#ifndef _WIN32
	typedef int SOCKET;
#endif

int sockInit(void) {
#ifdef _WIN32
		WSADATA wsa_data;
		return WSAStartup(MAKEWORD(1, 1), &wsa_data);
#else
		return 0;
#endif
}

int sockQuit(void) {
#ifdef _WIN32
		return WSACleanup();
#else
		return 0;
#endif
}

int sockClose(SOCKET sock) {
	int status = 0;
#ifdef _WIN32
	status = shutdown(sock, SD_BOTH);
	if (status == 0) { status = closesocket(sock); }
#else
	status = shutdown(sock, SHUT_RDWR);
	if (status == 0) { status = close(sock); }
#endif
	return status;
}

const short default_port = 8000;
const short max_listeners = 100;
const short max_file_name_len = 30;
const short buffer_size = 1024;

size_t min_(size_t a, size_t b) {
	if (a > b) return b;
	return a;
}

typedef char Byte;

class Receiver {
public:
	Receiver() = default;
	void Receive() { 
		wait_connection(); 
	};
private:
	void wait_connection() {
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == 0) {
			std::cerr << "Bad socket initialization" << std::endl;
			exit(1);
		}
		struct sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(default_port);
		int opt = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*) &opt, sizeof(opt))) {
			std::cerr << "Bad setsockopt" << std::endl;
			exit(1);
		}
		if (bind(sock, (struct sockaddr *)&address, sizeof(address))) {
			std::cerr << "Bad socket binding" << std::endl;
			exit(2);
		}
		if (listen(sock, 2)) {
			std::cerr << "Bad listen command" << std::endl;
			exit(3);
		}
		std::cout << "Waiting for connection" << std::endl;
		SOCKET connection = accept(sock, nullptr, nullptr);
		size_t threads = 0;
		recv(connection, (char*)&threads, sizeof(threads), 0);
		if (0 >= threads || threads > max_listeners) {
			std::cerr << "Get some bad data" << std::endl;
			exit(4);
		}
		Byte file_name[max_file_name_len];
		int name_len = recv(connection, file_name, max_file_name_len, 0);
		file_name[name_len] = '\0';
		FILE * to_create = fopen(file_name, "w");
		fclose(to_create);
		sockClose(connection);
		get_data(std::string(file_name), threads);
	}
	void get_data(const std::string& file_name, size_t threads_num) {
		std::vector<std::thread> threads;
		for (int i = 0; i < threads_num; i++) {
			SOCKET part_file = accept(sock, nullptr, nullptr);
			uint64_t offset = 0;
			recv(part_file, (char*)&offset, sizeof(offset), 0);
			FILE * file = fopen(file_name.c_str(), "a");
			if (!file) {
				std::cerr << "Bad out file open" << std::endl;
				exit(5);
			}
			if (fseek(file, offset, SEEK_SET)) {
				std::cerr << "Bad fseek" << std::endl;
				exit(5);
			}
			threads.push_back(std::thread(getter_and_writter, file, part_file));
		}
		for (auto& t : threads) {
			t.join();
		}
		std::cout << "Done" << std::endl;
		sockClose(sock);
	}
	static void getter_and_writter(FILE* destination, SOCKET source) {
		Byte buffer[buffer_size];
		int32_t readed;
		while ((readed = recv(source, buffer, sizeof(buffer), 0)) != 0) {
			fwrite(buffer, sizeof(Byte), readed, destination);
		}
		sockClose(source);
		fclose(destination);
	}

	SOCKET sock;
};

class Sender {
public:
	Sender(const std::string& file_name_, size_t threads_) : threads_num(threads_), file_name(file_name_) {};
	void Connect(const std::string& ip) {
		FILE* file = fopen(file_name.c_str(), "r");
		if (!file) {
			std::cout << "File does not exists" << std::endl;
			exit(1);
		}
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == 0) {
			std::cerr << "Bad socket initialization" << std::endl;
			exit(1);
		}
		struct sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = inet_addr(ip.c_str());
		address.sin_port = htons(default_port);
		if (connect(sock, (struct sockaddr *)&address, sizeof(address))) {
			std::cerr << "Bad socket connection on " << ip << std::endl;
			exit(2);
		}
		if (send(sock, (char*)&threads_num, sizeof(threads_num), 0) == -1) {
			std::cerr << "Bad send command" << std::endl;
			exit(3);
		}
		if (send(sock, file_name.c_str(), file_name.length(), 0) == -1) {
			std::cerr << "Bad send command" << std::endl;
			exit(4);
		}
		sockClose(sock);
		send_data(ip);
	};
private:
	uint64_t get_file_size() {
		FILE* file = fopen(file_name.c_str(), "r");
		fseek(file, 0, SEEK_END);
		uint64_t size = ftell(file);
		fclose(file);
		return size;
	}
	void send_data(const std::string& ip) {
		uint64_t one_portion_size = 1 + (get_file_size() - 1) / threads_num;
		std::vector<std::thread> threads;
		for (int i = 0; i < threads_num; i++) {
			uint64_t cur_offset = one_portion_size * i;
			SOCKET tmp_sock = socket(AF_INET, SOCK_STREAM, 0);
			if (tmp_sock == 0) {
				std::cerr << "Bad socket initialization" << std::endl;
				exit(1);
			}
			struct sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = inet_addr(ip.c_str());
			address.sin_port = htons(default_port);
			if (connect(tmp_sock, (struct sockaddr *)&address, sizeof(address))) {
				std::cerr << "Bad socket connection" << std::endl;
				exit(2);
			}
			if (send(tmp_sock, (char*)&cur_offset, sizeof(cur_offset), 0) == -1) {
				std::cerr << "Bad send command" << std::endl;
				exit(3);
			}
			FILE* file = fopen(file_name.c_str(), "r");
			if (fseek(file, one_portion_size * i, SEEK_SET)) {
				std::cerr << "Bad fseek" << std::endl;
				exit(5);
			};
			threads.push_back(std::thread(reader_and_sender, tmp_sock, file, one_portion_size));
		}
		for (auto& t : threads) {
			t.join();
		}
		std::cout << "Done" << std::endl;
	}
	static void reader_and_sender(SOCKET source, FILE* file, uint64_t one_portion_size) {
		Byte buffer[buffer_size];
		uint64_t count = 0;
		int readed;
		while ((readed = fread(buffer, sizeof(Byte), min_(one_portion_size - count, sizeof(buffer)), file)) > 0 && count < one_portion_size) {
			send(source, buffer, readed, 0);
			count += readed;
		}
		fclose(file);
		sockClose(source);
	}
	std::string file_name;
	size_t threads_num;
	SOCKET sock;
};


int main(int argc, char* argv[]) {
	sockInit();
	if (argc == 2) {
		if (std::string(argv[1]) == "-receive") {
			Receiver().Receive();
		}
		else {
			std::cout << "Wrong arguments" << std::endl;
		}
		return 0;
	}
	if (argc == 8) {
		std::unordered_map<std::string, std::string> args;
		args["-send"] = "";
		args["-ip"] = "";
		args["-file"] = "";
		args["-threads"] = "";
		for (int i = 1; i < 7; i++) {
			if (argv[i][0] == '-') {
				args[argv[i]] = argv[i + 1];
			}
		}
		if (args.size() == 4 && args["-threads"].length() && args["-send"].length() && args["-ip"].length() && args["-file"].length()) {
			int threads = std::stoi(args["-threads"]);
			if (threads < 0 || threads > max_listeners) {
				std::cout << "Invalid ammount of threads" << std::endl;
				sockQuit();
				return 0;
			}
			Sender(args["-file"], threads).Connect(args["-ip"]);
		}
		else {
			std::cout << "Bad arguments given" << std::endl;
		}
		sockQuit();
		return 0;
	}
	std::cout << "Wrong ammout of arguments" << std::endl;
	sockQuit();
    return 0;
}

