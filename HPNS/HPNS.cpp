/*
MIT License

Copyright (c) 2024 HalfPeopleStudio Inc - Half People
@birth: created by HalfPeople on 2024-12-22
@version: V1.5.0
@revision: last revised by HalfPeople on 2024-12-22

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.



*/

#include "HPNS.h"
#include <string>
#include <unordered_map>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>


#ifndef _WIN32
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <exception>
#include <cstdarg>
#include <cstdio>
typedef struct sockaddr HSOCKADDR;
#define HSOCKET_ERROR -1
#define GetLastError() errno
#define closesocket close
#define ioctlsocket ioctl
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"Ws2_32.lib")
#define HSOCKET_ERROR SOCKET_ERROR
typedef SOCKADDR HSOCKADDR;
#endif // _WIN32

#define HINVALID_SOCKET (HPNS::ConnectDevice)(~0)
HPNS::Context::HContext* global_context = nullptr;


#if HPNS_LOG_ACTIVATE

std::mutex log_mutex;

void HPNS_LOG_CALLBACK(HPNS::Internal::Base_NetworkObject* ns, const char* message, int line, const char* function, ...)
{
	std::lock_guard lock(log_mutex);
	if (!ns->SetupCallback().LogMessage)
		return;
	std::string buffer_msg = std::string("HPNS Log System - HPNS.cpp, line:").append(std::to_string(line)).append(" : ").append(function).append(" message :");
	// 使用變參來格式化錯誤信息
	va_list args;
	va_start(args, message);

	// 生成錯誤信息
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), message, args);
	buffer_msg.append(buffer);

	va_end(args);

	ns->SetupCallback().LogMessage(buffer_msg, ns);
}
void HPNS_LOG_CALLBACK_G(const char* message, int line, const char* function, ...)
{
	std::lock_guard lock(log_mutex);
	if (global_context==nullptr || !global_context->Subsystem_LogMessage_callback)
		return;
	std::string buffer_msg = std::string("HPNS Log System - HPNS.cpp, line:").append(std::to_string(line)).append(" : ").append(function).append(" message :");
	// 使用變參來格式化錯誤信息
	va_list args;
	va_start(args, message);

	// 生成錯誤信息
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), message, args);
	buffer_msg.append(buffer);

	va_end(args);

	global_context->Subsystem_LogMessage_callback(buffer_msg);
}

#define HPNS_LOG(network_system,message,...) HPNS_LOG_CALLBACK(network_system,message,__LINE__,__FUNCTION__ ,__VA_ARGS__ )
#define HPNS_LOG_G(message,...) HPNS_LOG_CALLBACK_G(message,__LINE__,__FUNCTION__ ,__VA_ARGS__ )
#else
#define HPNS_LOG(network_system,message,...)
#endif // HPNS_LOG_ACTIVATE


//-----------------------------------------------------------------------------------------
//Exception
class HPNS_Exception : public std::exception
{
public:
	HPNS_Exception(const char* format, ...)
	{
		// 使用變參來格式化錯誤信息
		va_list args;
		va_start(args, format);

		// 生成錯誤信息
		char buffer[256];
		vsnprintf(buffer, sizeof(buffer), format, args);
		message.append(buffer);

		va_end(args);
	}
	virtual const char* what() const noexcept override
	{
		return message.c_str();
	}
	std::string message = "HPNS -  ServerException : ";
};


#define COMMAND_LIST (*static_cast<std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)>>*>(command_list))
#define ToCOMMAND_LIST(cmd_list) (*static_cast<std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)>>*>(cmd_list))

//-----------------------------------------------------------------------------------------
//BaseNetworkSystemClass
void HPNS::Internal::Base_NetworkObject::Start(bool thread_activate)
{
	HPNS_LOG(this, "Start the network system", 0);
	if (working)
	{
		HPNS_LOG(this, "The network system has been started", 0);
		return;
	}
	working = true;

	if (thread_activate && working_thread == nullptr)
	{
		HPNS_LOG(this, "Multithreading enabled", 0);
		HPNS::Context::GetCurrentContext()->current_thread_count++;
		if (global_context->current_thread_count > global_context->max_thread_count)
		{
			throw HPNS_Exception("Maximum number of threads exceeded  (超出最大綫程數)   %d/%d", global_context->current_thread_count, global_context->max_thread_count);
			global_context->current_thread_count--;
			return;
		}
		working_thread = new std::thread([&]() {
			HPNS_LOG(this, "Create worker thread", 0);
			while (working) {
				try
				{
#if HPNS_LOG_ACTIVATE
					if(callbacks.ShowUpdataLog)
						HPNS_LOG(this, "worker thread update", 0);
#endif // 

					Update();
				}
				catch (const std::exception& err)
				{
					callbacks.NetworkSystemThreadExistError(err.what(), this);
				}

			}
			});
	}
	else
		while (working) { HPNS_LOG(this, "main thread update", 0); Update(); }
}
void HPNS::Internal::Base_NetworkObject::Close()
{
	HPNS_LOG(this, "Shut down the network system", 0);
	working = false; if (working_thread != nullptr) { Client_CloseClientConnet(); HPNS_LOG(this, "Wait for the worker thread to end", 0); static_cast<std::thread*>(working_thread)->join(); delete working_thread; working_thread = nullptr; HPNS::Context::GetCurrentContext()->current_thread_count--; }
}
void HPNS::Internal::Base_NetworkObject::push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*, HPNS::ConnectDevice)> function, bool cover)
{
	HPNS_LOG(this, "Push command : %s", command_name);
	if (cover || !command_is_exist(command_name))
		COMMAND_LIST[command_name] = function;
	else
		throw HPNS_Exception("A command with the same name already exists (command name :%s) (已存在同名的命令)", command_name);
}
void HPNS::Internal::Base_NetworkObject::erase_command(const char* command_name)
{
	HPNS_LOG(this, "Erase command : %s", command_name);
	if (command_is_exist(command_name))
		COMMAND_LIST.erase(command_name);
	else
		throw HPNS_Exception("Requesting to erase a non-existent command  請求刪除不存在的命令  (command name : %s)", command_name);
}
bool HPNS::Internal::Base_NetworkObject::command_is_exist(const char* command_name)
{
	bool buff = COMMAND_LIST.find(command_name) != COMMAND_LIST.end();
	HPNS_LOG(this, "command \"%s\" is exist : %s", command_name, buff ? "true" : "false");
	return buff;
}
bool HPNS::Internal::Base_NetworkObject::call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device)
{
	HPNS_LOG(this, "Call command : %s", command_name.c_str());
	if (!global_context->call_command(command_name, data, this, device))
	{
		auto command = COMMAND_LIST.find(command_name);
		if (command != COMMAND_LIST.end())
		{
			HPNS_LOG(this, "Command %s in the current network system", command_name.c_str());
			command->second(data, this, device);
		}
		else
		{
			HPNS_LOG(this, "Command %s not found", command_name.c_str());
			return false;
		}
	}
	return true;
}
void HPNS::Internal::Base_NetworkObject::insert_commands(Base_NetworkObject* network_object, bool cover)
{
	if (network_object == nullptr)
	{
		throw HPNS_Exception("insert commands context is null  插入命令上下文為空");
		return;
	}

	if (cover)
	{
		HPNS_LOG(this, "Overwrite mode copies other network system commands to the current network system", 0);
		for (auto& command : ToCOMMAND_LIST(network_object->command_list))
			COMMAND_LIST[command.first] = command.second;
	}
	else
	{
		HPNS_LOG(this, "Non-overwrite mode copies other network system commands to the current network system", 0);
		for (auto& command : ToCOMMAND_LIST(network_object->command_list)) {
			if (COMMAND_LIST.find(command.first) == COMMAND_LIST.end())
				COMMAND_LIST[command.first] = command.second;
		}
	}
}
void HPNS::Internal::Base_NetworkObject::clear_command() { HPNS_LOG(this, "Clean up all commands in the current network system", 0); COMMAND_LIST.clear(); }

void HPNS::Internal::Base_NetworkObject::init_command_list()
{
	if (command_list == nullptr)
	{
		command_list = new std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*, HPNS::ConnectDevice)>>;
		HPNS_LOG(this, "Create internal command list : %x", command_list);
	}
	else
	{
		HPNS_LOG(this, "The internal command list \"%x\" already exists and there is no need to create it again.", command_list);
	}
}
void HPNS::Internal::Base_NetworkObject::release_command_list()
{
	if (command_list != nullptr)
	{
		HPNS_LOG(this, "Release internal command list : %x", command_list);
		delete command_list;
		command_list = nullptr;
	}
	else
	{
		HPNS_LOG(this, "The internal command list has been released : %x", command_list);
	}
}

//-----------------------------------------------------------------------------------------
// Server
//-----------------------------------------------------------------------------------------
#if HPNS_SERVER_ACTIVATE

//-----------------------------------------------------------------------------------------
//---------------------------------------------TCP_IP4-------------------------------------
struct TCP_IP4_Server_Context
{
#ifdef _WIN32
	WSADATA wd;
#endif // _WIN32
	struct sockaddr_in addr;
	HPNS::ConnectDevice sListen;
	fd_set  readSet, tmpSet;//定义一个读（接受消息）的集合
	int thread_count_ = 0;
};


#define TCP_IP4_CTX (*static_cast<TCP_IP4_Server_Context*>(network_context))
HPNS::Server::TCP_IP4::TCP_IP4(HPort port, const char* ip, int thread_count)
{
	init_command_list();
	if (network_context == nullptr)
	{
		network_context = new TCP_IP4_Server_Context;
		HPNS_LOG(this, "Create internal network_context : %x", network_context);
	}
	else
	{
		HPNS_LOG(this, "The internal network_context \"%x\" already exists and there is no need to create it again.", network_context);
	}

	if (thread_count < 0 && HPNS::Context::GetCurrentContext()->current_thread_count < global_context->max_thread_count)
	{
		HPNS_LOG(this, "The specified number of threads is less than 0. Add the maximum number of threads to the thread pool.", 0);
		global_context->thread_pool.push_thread(global_context->max_thread_count - global_context->current_thread_count);
	}
	else
	{
		HPNS_LOG(this, "Adds the specified number of threads %d to the threadpool", thread_count);
		global_context->thread_pool.push_thread(thread_count);
		TCP_IP4_CTX.thread_count_ = thread_count;
	}

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &TCP_IP4_CTX.wd) == HSOCKET_ERROR)
	{
		throw HPNS_Exception("WSAStartup  error code : %d", GetLastError());
		return;
	}

	HPNS_LOG(this, "window platform WSAStartup");
#endif // _WIN32


	TCP_IP4_CTX.sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	HPNS_LOG(this, "create tcp ipv4 socket", 0);
	if (TCP_IP4_CTX.sListen == HINVALID_SOCKET)
	{
		throw HPNS_Exception("socket error code : %d", GetLastError());
		return;
	}

	//#ifndef _WIN32
	//	setsockopt(TCP_IP4_CTX.sListen, SOL_SOCKET, SO_REUSEADDR, &TCP_IP4_CTX.opt, sizeof(TCP_IP4_CTX.opt));
	//#endif // _WIN32

#ifdef _WIN32
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
	TCP_IP4_CTX.addr.sin_addr.s_addr = inet_addr(ip);
#else
	if (inet_pton(AF_INET, ip, &TCP_IP4_CTX.addr.sin_addr) <= 0) {
		throw HPNS_Exception("Invalid address/ Address not supported");
		return;
	}
#endif // _WINSOCK_DEPRECATED_NO_WARNINGS
#else
	TCP_IP4_CTX.addr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr(ip);
#endif // _WIN32
	TCP_IP4_CTX.addr.sin_family = AF_INET;
	TCP_IP4_CTX.addr.sin_port = htons(port);

	HPNS_LOG(this, "setup server address : %s:%d", ip, port);
}


void HPNS::Server::TCP_IP4::Listen()
{
	int len = sizeof(HSOCKADDR);
	if (bind(TCP_IP4_CTX.sListen, (HSOCKADDR*)&TCP_IP4_CTX.addr, len) == HSOCKET_ERROR)
	{
		throw HPNS_Exception("bind  error: %d", GetLastError());
		return;
	}
	HPNS_LOG(this, "bind socket and address : %d", TCP_IP4_CTX.sListen);
	if (listen(TCP_IP4_CTX.sListen, 5) == HSOCKET_ERROR)
	{
		throw HPNS_Exception("listen  error: %d", GetLastError());
		return;
	}
	HPNS_LOG(this, "setup server listen", 0);

	FD_ZERO(&TCP_IP4_CTX.readSet);//初始化集合
	FD_SET(TCP_IP4_CTX.sListen, &TCP_IP4_CTX.readSet);
	HPNS_LOG(this, "setup server read fd_set", 0);
}

HPNS::Server::TCP_IP4::~TCP_IP4()
{
	HPNS_LOG(this, "Network system release", 0);
	if (working) {
		HPNS_LOG(this, "The network system is running and is requesting to be shut down.", 0);
		Close();
	}
	release_command_list();
#if HPNS_LOG_ACTIVATE
	HPNS_LOG(this, "Close the network system socket : %d", closesocket(TCP_IP4_CTX.sListen));
#else
	closesocket(TCP_IP4_CTX.sListen);
#endif

	HPNS_LOG(this, "The number of threads required before closing %d", TCP_IP4_CTX.thread_count_);
	global_context->thread_pool.pop_thread(TCP_IP4_CTX.thread_count_);
	if (network_context != nullptr)
	{
		delete network_context;
		network_context = nullptr;
		HPNS_LOG(this, "release network system : %x", network_context);
	}
	if (message_buffer != nullptr)
	{
		delete message_buffer;
		message_buffer = nullptr;
		HPNS_LOG(this, "message_buffer released for user use : %x", message_buffer);
	}
	else
	{
		HPNS_LOG(this, "The user has not used the message_buffer and does not need to release it.", 0);
	}
}

void HPNS::Server::TCP_IP4::Update()
{
	TCP_IP4_CTX.tmpSet = TCP_IP4_CTX.readSet;
	struct timeval timeout; timeout.tv_sec = 2;
	timeout.tv_usec = 0;
#if HPNS_LOG_ACTIVATE
	if (callbacks.ShowUpdataLog)
		HPNS_LOG(this, "Reset data and wait for \"select\"", 0);
#endif // 
	

	int ret = select(TCP_IP4_CTX.sListen + 1, &TCP_IP4_CTX.tmpSet, NULL, NULL, &timeout);
	if (ret < 1) {
#if HPNS_LOG_ACTIVATE
		if (callbacks.ShowUpdataLog)
			HPNS_LOG(this, "select return : %d (%s).leave this update", ret, ret == 0 ? "timeout" : "error");
#endif // 
			
		return;
	}

#ifdef _WIN32
#if HPNS_LOG_ACTIVATE
	if (callbacks.ShowUpdataLog)
		HPNS_LOG(this, "window platform loop tmp fd_set get information");
#endif // 
	
	for (size_t i = 0; i < TCP_IP4_CTX.tmpSet.fd_count; i++)
#else
#if HPNS_LOG_ACTIVATE
	if (callbacks.ShowUpdataLog)
		HPNS_LOG(this, "uinx platform loop FD_SETSIZE (%d) get information", FD_SETSIZE);
#endif // 
	
	for (HPNS::ConnectDevice client_connet = 0; client_connet < FD_SETSIZE; client_connet++)
#endif // _WIN32
	{

#ifdef _WIN32
		HPNS::ConnectDevice client_connet = TCP_IP4_CTX.tmpSet.fd_array[i];
#endif // !_WIN32
		if (!FD_ISSET(client_connet, &TCP_IP4_CTX.tmpSet)) {
			continue;
		}

		HPNS_LOG(this, "Socket %d has information updates", client_connet);

		if (TCP_IP4_CTX.sListen == client_connet)
		{
			HPNS_LOG(this, "Socket is judged as a new user just like local listening Socket.", 0);
			//struct sockaddr_in client_address;
			//socklen_t client_len = sizeof(client_address);

			HPNS::ConnectDevice client = accept(TCP_IP4_CTX.sListen, 0, 0);//(HSOCKADDR*)&client_address, &client_len);
			if (client >= 0)
			{
#ifdef _WIN32
				if (TCP_IP4_CTX.readSet.fd_count < FD_SETSIZE)
				{
					HPNS_LOG(this, "(Window platform) Successfully accepted connet");
					FD_SET(client, &TCP_IP4_CTX.readSet);

					if (callbacks.ClientEntry)
						callbacks.ClientEntry(client, this);
				}
				else
				{
					HPNS_LOG(this, "(Window platform) Successfully accepted connet.But the set space is not enough to add more clients connet");
					if (callbacks.ServerCapacityExceeded)
						callbacks.ServerCapacityExceeded(client, this);
					//closesocket(client);
					throw HPNS_Exception("The number of clients is greater than the server client capacity (客戶端數量大於伺服器客戶端容量)");
				}
#else
				HPNS_LOG(this, "(Unix platform) Successfully accepted connet.", 0);
				FD_SET(client, &TCP_IP4_CTX.readSet);

				if (callbacks.ClientEntry)
					callbacks.ClientEntry(client, this);
#endif // _WIN32
			}
			else
			{
				HPNS_LOG(this, "Accept connet errors code : %d", client);
			}
		}
		else
		{
#ifdef _WIN32
			u_long nread = 0;
			ioctlsocket(client_connet, FIONREAD, &nread);
#else
			int nread = 0;
			ioctl(client_connet, FIONREAD, &nread);
#endif // _WIN32


			HPNS_LOG(this, "Get information status (ioctl) :%d", nread);

			if (nread == 0)
			{
				HPNS_LOG(this, "client leaves normally", 0);
				if (callbacks.ClientLeaves)
					callbacks.ClientLeaves(client_connet, this);
				closesocket(client_connet);
				FD_CLR(client_connet, &TCP_IP4_CTX.readSet);
			}
			else
			{
				HPNS_LOG(this, "Process message", 0);
				std::vector<char> buffer(HPNS_RECV_BUFFER_SIZE);
				ret = recv(client_connet, buffer.data(), HPNS_RECV_BUFFER_SIZE, 0);
				if (ret == HSOCKET_ERROR || ret <= 0)
				{
					HPNS_LOG(this, "There is an error in receiving information or the client leaves normally : %d(%s)", ret, ret == 0 ? "Leave normally" : "Error");

					if (callbacks.ClientLeaves)
						callbacks.ClientLeaves(client_connet, this);
					closesocket(client_connet);
					FD_CLR(client_connet, &TCP_IP4_CTX.readSet);
				}
				else
				{
					try
					{
						buffer.resize(ret);
						if (callbacks.ReceiveMessageDecryption)
							callbacks.ReceiveMessageDecryption(buffer);
						if (callbacks.ReceiveMessageDecryption_Ex)
							callbacks.ReceiveMessageDecryption_Ex(buffer, client_connet, this);

						HPNS_LOG(this, "The message is decrypted and submitted to the thread pool task queue", 0);
						nlohmann::json message = nlohmann::json::from_msgpack(buffer, ret);
						global_context->thread_pool.push_task(message["cmd"].get<std::string>(), message["data"], this, client_connet);
					}
					catch (const nlohmann::json::exception& err)
					{
						throw HPNS_Exception("recv message json unpack error : %s", err.what());
					}

				}
			}
		}
	}
}


bool HPNS::Server::TCP_IP4::MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data)
{
	HPNS_LOG(this, "Send information to target device(%d) - command : %s", device, command_name);
	nlohmann::json message;
	std::vector<char> msg_buffer;
	try
	{
		if (!data.is_null())
			message["data"] = data;
		message["cmd"] = command_name;
		nlohmann::json::to_msgpack(message, msg_buffer);
		message.clear();
	}
	catch (const nlohmann::json::exception& err)
	{
		throw HPNS_Exception("SendMessageToClient Json error : %s", err.what());
		return false;
	}
	if (callbacks.SendMessageEncrypted)
		callbacks.SendMessageEncrypted(msg_buffer);
	if (callbacks.SendMessageEncrypted_Ex)
		callbacks.SendMessageEncrypted_Ex(msg_buffer, device, this);
	return send(device, msg_buffer.data(), msg_buffer.size(), 0) > 0;
}

const char* HPNS::Server::TCP_IP4::MSG_GetDeviceIP(HPNS::ConnectDevice device)
{
	sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == HSOCKET_ERROR) {
		throw HPNS_Exception("getsockname failed.");
	}
	else {
#ifdef _WIN32
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
		char* dt = inet_ntoa(localAddr.sin_addr);
		HPNS_LOG(this, "(Window) Get device(%d) IP : %s", device, dt);
		return dt;
#else
		char ipStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, sizeof(ipStr)) != nullptr) {
			std::string buff = ipStr;
			HPNS_LOG(this, "(Window) Get device(%d) IP : %s", device, buff.c_str());
			return buff.data();
		}
		else {
			HPNS_LOG(this, "(Window) An error occurred while obtaining the device IP");
			return "inet_ntop failed";
		}
#endif // _WINSOCK_DEPRECATED_NO_WARNINGS
#else
		char* dt = inet_ntoa(localAddr.sin_addr);
		HPNS_LOG(this, "(Uinx)Get device(%d) IP : %s", device, dt);
		return dt;
#endif // _WIN32


	}
	HPNS_LOG(this, " An error occurred while obtaining the device IP", 0);
	return "null";
}

bool HPNS::Server::TCP_IP4::MSG_CloseClientConnet(HPNS::ConnectDevice device)
{
	if (callbacks.ClientLeaves)
		callbacks.ClientLeaves(device, this);
	closesocket(device);
	FD_CLR(device, &TCP_IP4_CTX.readSet);

	HPNS_LOG(this, "Close device(%d) connect", device);
	return true;
}

#endif
//-----------------------------------------------------------------------------------------
// Client
//-----------------------------------------------------------------------------------------
#if HPNS_CLIENT_ACTIVATE
struct TCP_IP4_Client_Context
{
#ifdef _WIN32
	WSADATA wd;
#endif
	HPNS::ConnectDevice Server;
	struct sockaddr_in addr;
	int thread_count_ = 0;
};

#define TCP_IP4_CTX_CLI (*static_cast<TCP_IP4_Client_Context*>(network_context))
HPNS::Client::TCP_IP4::TCP_IP4(const char* ip, HPort port, int thread_count)
{
	init_command_list();
	if (network_context == nullptr) {
		network_context = new TCP_IP4_Client_Context;
		HPNS_LOG(this, "Create client network_context :%x", network_context);
	}

	if (thread_count < 0 && HPNS::Context::GetCurrentContext()->current_thread_count < global_context->max_thread_count)
	{
		global_context->thread_pool.push_thread(global_context->max_thread_count - global_context->current_thread_count);
		HPNS_LOG(this, "The specified number of threads is less than 0. Add the maximum number of threads to the thread pool.", 0);
	}
	else
	{
		global_context->thread_pool.push_thread(thread_count);
		TCP_IP4_CTX.thread_count_ = thread_count;
		HPNS_LOG(this, "Adds the specified number of threads %d to the threadpool", thread_count);
	}

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &TCP_IP4_CTX_CLI.wd) != 0)
	{
		throw HPNS_Exception("WSAStartup  error code : %d", GetLastError());
		return;
	}
	HPNS_LOG(this, "window platform WSAStartup");
#endif // _WIN32



	TCP_IP4_CTX_CLI.addr.sin_family = AF_INET;
	TCP_IP4_CTX_CLI.addr.sin_port = htons(port);
#ifdef _WIN32
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
	TCP_IP4_CTX_CLI.addr.sin_addr.s_addr = inet_addr(ip);
#else
	if (inet_pton(AF_INET, ip, &TCP_IP4_CTX.addr.sin_addr) <= 0) {
		throw HPNS_Exception("Invalid address/ Address not supported");
		return;
	}
#endif // _WINSOCK_DEPRECATED_NO_WARNINGS
#else
	TCP_IP4_CTX_CLI.addr.sin_addr.s_addr = inet_addr(ip);
#endif // DEBUG
	HPNS_LOG(this, "setup server address : %s:%d", ip, port);
}

HPNS::Client::TCP_IP4::~TCP_IP4()
{
	HPNS_LOG(this, "Network system release", 0);
	if (working) { HPNS_LOG(this, "The network system is running and is requesting to be shut down.", 0); Close(); }
	release_command_list();
#if HPNS_LOG_ACTIVATE
	HPNS_LOG(this, "Close the network system socket : %d", closesocket(TCP_IP4_CTX_CLI.Server));
#else
	closesocket(TCP_IP4_CTX_CLI.Server);
#endif

	HPNS_LOG(this, "The number of threads required before closing %d", TCP_IP4_CTX_CLI.thread_count_);
	global_context->thread_pool.pop_thread(TCP_IP4_CTX_CLI.thread_count_);
	if (network_context != nullptr)
	{
		delete network_context;
		network_context = nullptr;
		HPNS_LOG(this, "release network system : %x", network_context);
	}
	if (message_buffer != nullptr)
	{
		delete message_buffer;
		message_buffer = nullptr;
		HPNS_LOG(this, "message_buffer released for user use : %x", message_buffer);
	}
	else
	{
		HPNS_LOG(this, "The user has not used the message_buffer and does not need to release it.", 0);
	}
}

bool HPNS::Client::TCP_IP4::Connect()
{
	TCP_IP4_CTX_CLI.Server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (TCP_IP4_CTX_CLI.Server == HINVALID_SOCKET)
	{
		throw HPNS_Exception("socket  error code : %d", GetLastError());
		return false;
	}
	HPNS_LOG(this, "create socket : %d", TCP_IP4_CTX_CLI.Server);
	HPNS_LOG(this, "connect server", 0);
	int len = sizeof(struct sockaddr_in);
	if (connect(TCP_IP4_CTX_CLI.Server, (HSOCKADDR*)&TCP_IP4_CTX_CLI.addr, len) == HSOCKET_ERROR)
	{
		throw HPNS_Exception("connect  error : %d", GetLastError());
		return false;
	}

	if (callbacks.ClientEntry)
		callbacks.ClientEntry(TCP_IP4_CTX_CLI.Server, this);
	return true;
}

void HPNS::Client::TCP_IP4::Update()
{
	std::vector<char> buffer(HPNS_RECV_BUFFER_SIZE);
	int ret = recv(TCP_IP4_CTX_CLI.Server, buffer.data(), HPNS_RECV_BUFFER_SIZE, 0);
	if (ret == 0 || ret == HSOCKET_ERROR)
	{
		HPNS_LOG(this, "recv disconnected from the server or an error occurred : %d(%s)", ret, ret == 0 ? "disconnected" : "error");
		//Client_CloseClientConnet();
		return;
	}

	try
	{
		buffer.resize(ret);
		if (callbacks.ReceiveMessageDecryption)
			callbacks.ReceiveMessageDecryption(buffer);
		if (callbacks.ReceiveMessageDecryption_Ex)
			callbacks.ReceiveMessageDecryption_Ex(buffer, TCP_IP4_CTX_CLI.Server, this);
		HPNS_LOG(this, "The message is decrypted and submitted to the thread pool task queue", 0);
		nlohmann::json message = nlohmann::json::from_msgpack(buffer, ret);
		global_context->thread_pool.push_task(message["cmd"].get<std::string>(), message["data"], this, TCP_IP4_CTX_CLI.Server);
	}
	catch (const nlohmann::json::exception& err)
	{
		throw HPNS_Exception("recv message json error : %s", err.what());
	}
}

bool HPNS::Client::TCP_IP4::MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data)
{
	nlohmann::json message;
	std::vector<char> msg_buffer;
	try
	{
		if (!data.is_null())
			message["data"] = data;
		message["cmd"] = command_name;
		nlohmann::json::to_msgpack(message, msg_buffer);
		message.clear();
	}
	catch (const nlohmann::json::exception& err)
	{
		throw HPNS_Exception("SendMessageToServer Json error : %s", err.what());
		return false;
	}
	if (callbacks.SendMessageEncrypted)
		callbacks.SendMessageEncrypted(msg_buffer);
	if (callbacks.SendMessageEncrypted_Ex)
		callbacks.SendMessageEncrypted_Ex(msg_buffer, device, this);
	return send(device, msg_buffer.data(), msg_buffer.size(), 0) > 0;
}

const char* HPNS::Client::TCP_IP4::MSG_GetDeviceIP(HPNS::ConnectDevice device)
{
	struct sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == HSOCKET_ERROR) {
		throw HPNS_Exception("getsockname failed.");
	}
	else {
#ifdef _WIN32
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
		return inet_ntoa(localAddr.sin_addr);
#else
		char ipStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, sizeof(ipStr)) != nullptr) {
			printf("IP Address: %s", ipStr);
			std::string buff = ipStr;
			return buff.data();
		}
		else {
			return "inet_ntop failed";
		}
#endif // _WINSOCK_DEPRECATED_NO_WARNINGS
#else
		return inet_ntoa(localAddr.sin_addr);
#endif // _WIN32


	}
	return "null";
}

bool HPNS::Client::TCP_IP4::MSG_CloseClientConnet(HPNS::ConnectDevice device)
{
	if (callbacks.ClientLeaves)
		callbacks.ClientLeaves(device, this);

	closesocket(device);
	return true;
}

bool HPNS::Client::TCP_IP4::MSG_IsConnected(HPNS::ConnectDevice device)
{
	sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == HSOCKET_ERROR) {
		return false;
	}


	return true;
}

bool HPNS::Client::TCP_IP4::Client_IsConnected()
{
	return MSG_IsConnected(TCP_IP4_CTX_CLI.Server);
}

bool HPNS::Client::TCP_IP4::Client_SendMessageToServer(const char* command_name, nlohmann::json data)
{
	return MSG_SendMessageToClient(TCP_IP4_CTX_CLI.Server, command_name, data);
}

const char* HPNS::Client::TCP_IP4::Client_GetDeviceIP()
{
	return MSG_GetDeviceIP(TCP_IP4_CTX_CLI.Server);
}

bool HPNS::Client::TCP_IP4::Client_CloseClientConnet()
{
	return MSG_CloseClientConnet(TCP_IP4_CTX_CLI.Server);
}





#endif // HPNS_CLIENT_ACTIVATE
//-------------------------------------------------------------------------------------------
// Global
//-------------------------------------------------------------------------------------------



//-----------------------------------------------------------------------------------------
// context -- function

HPNS::Context::HContext* HPNS::Context::CreateContext()
{
	if (global_context == nullptr)
		global_context = new HPNS::Context::HContext;

	return global_context;
}

HPNS::Context::HContext* HPNS::Context::GetCurrentContext()
{
	if (global_context == nullptr)
		CreateContext();
	HPNS_LOG_G("GetCurrentContext : %x",global_context);
	return global_context;
}

void HPNS::Context::SetCurrentContext(HContext* new_context)
{
	HPNS_LOG_G("SetCurrentCobtext \"%x\" To NewContext \"%x\"", global_context,new_context);
	if (new_context == nullptr)
		throw HPNS_Exception("\"new_context\" is empty (\"new_context\" 爲空)");
	if (global_context != nullptr)
		ReleaseContext();


	global_context = new_context;
}

void HPNS::Context::ReleaseContext()
{
	if (global_context != nullptr)
	{
		HPNS_LOG_G("Release current Context %x", global_context);
		delete global_context;
		global_context = nullptr;
	}
	else
	{
		HPNS_LOG_G("current Context is empty and does not need to be released",0);
	}
}

void HPNS::Context::ReleaseContext(HContext* target_context)
{
	if (target_context != nullptr)
	{
		HPNS_LOG_G("Release Context %x", global_context);
		delete target_context;
		target_context = nullptr;
	}
	else
	{
		HPNS_LOG_G("Context is empty and does not need to be released",0);
	}
}



//-----------------------------------------------------------------------------------------
// HContext class

void HPNS::Context::HContext::init_command_list()
{
	if (command_list == nullptr) {
		HPNS_LOG_G("Create context command list",0);
		command_list = new std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*, HPNS::ConnectDevice)>>;
	}
	else
	{
		HPNS_LOG_G("Create context command list.already exists",0);
	}
}

void HPNS::Context::HContext::release_command_list()
{
	if (command_list != nullptr)
	{
		HPNS_LOG_G("release context command list : 0x%x",command_list);
		delete command_list;
		command_list = nullptr;
	}
	else
	{
		HPNS_LOG_G("context command list has been released : 0x%x", command_list);
	}
}

HPNS::Context::HContext::HContext()
{
	init_command_list();
	max_thread_count = std::thread::hardware_concurrency();
	HPNS_LOG_G("Get the maximum available threads : %d", max_thread_count);
}

void HPNS::Context::HContext::push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*, HPNS::ConnectDevice)> function, bool cover)
{
	init_command_list();

	if (cover || !command_is_exist(command_name)) {
		HPNS_LOG_G("Add command \"%s\" to the list", command_name);
		COMMAND_LIST[command_name] = function;
	}
	else
		throw HPNS_Exception("A command with the same name already exists (command name :%s) (已存在同名的命令)", command_name);
}

void HPNS::Context::HContext::erase_command(const char* command_name)
{
	if (command_is_exist(command_name)) {
		HPNS_LOG_G("erase command \"%s\" at the list", command_name);
		COMMAND_LIST.erase(command_name);
	}
	else
		throw HPNS_Exception("Requesting to erase a non-existent command  請求刪除不存在的命令  (command name : %s)", command_name);
}

bool HPNS::Context::HContext::command_is_exist(const char* command_name)
{
#if HPNS_LOG_ACTIVATE
	bool b = COMMAND_LIST.find(command_name) != COMMAND_LIST.end();
	HPNS_LOG_G("Check the command \"%s\" is \"%s\" in the command list", command_name, b ? "exist" : "not exist");
	return b;
#else
	return COMMAND_LIST.find(command_name) != COMMAND_LIST.end();
#endif // HPNS
}

bool HPNS::Context::HContext::call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device)
{
	auto command = COMMAND_LIST.find(command_name);
	if (command != COMMAND_LIST.end()) {
		HPNS_LOG_G("call command \"%s\"", command_name);
		command->second(data, network_system, device);
	}
	else
		return false;//throw HPNS_Exception("Using non-existent command  正在使用不存在的命令  (command name : %s)", command_name);
	return true;
}

void HPNS::Context::HContext::insert_commands(HContext* context, bool cover)
{
	if (context == nullptr)
	{
		throw HPNS_Exception("insert commands context is null  插入命令上下文為空");
		return;
	}


	if (cover)
	{
		HPNS_LOG_G("Use overwrite mode to insert all commands in context \"%x\" into the current context \"%x\"", context, this);
		for (auto& command : ToCOMMAND_LIST(context->command_list))
			COMMAND_LIST[command.first] = command.second;
	}
	else
	{
		HPNS_LOG_G("Non-overwriting mode inserts all commands in context \"%x\" into the current context \"%x\"", context, this);
		for (auto& command : ToCOMMAND_LIST(context->command_list)) {
			if (COMMAND_LIST.find(command.first) == COMMAND_LIST.end())
				COMMAND_LIST[command.first] = command.second;
		}
	}
}

void HPNS::Context::HContext::clear_command() { HPNS_LOG_G("Clear all commands in the current context.", 0); COMMAND_LIST.clear(); }

std::vector<std::string> HPNS::Context::HContext::get_all_command()
{
//#if HPNS_LOG_ACTIVATE
//	std::string buff;
//#endif // HPNS_LOG_ACTIVATE

	std::vector<std::string> commands;
	for (auto& cmd : COMMAND_LIST) {
		commands.push_back(cmd.first);
//#if HPNS_LOG_ACTIVATE
//		buff.append(",\"").append(cmd.first).append("\"");
//#endif // HPNS_LOG_ACTIVATE
	}

//#if HPNS_LOG_ACTIVATE
//	HPNS_LOG_G("get all command : { %s }", buff.c_str());
//#endif
	return commands;
}

//-----------------------------------------------------------------------------------------
// Thread pool

#define TPCtx (*static_cast<ThreadPoolContext*>(thread_pool_context))

struct ThreadPoolContext
{
	std::queue<HPNS::Context::Task> task_list;
	std::mutex thread_pool_mutex;
	std::condition_variable cv;
	std::map<std::thread::id, std::thread*> thread_list;
	std::vector<std::thread*> remove_list;
	int close_thread_count = 0;
};

HPNS::Context::ThreadPool::ThreadPool()
{
	if (thread_pool_context == nullptr)
		thread_pool_context = new ThreadPoolContext;
}

HPNS::Context::ThreadPool::~ThreadPool()
{
	if (thread_pool_context != nullptr)
	{
		HPNS_LOG_G("Release thread pool context : %x", thread_pool_context);
		delete thread_pool_context;
		thread_pool_context = nullptr;
	}
	else
	{
		HPNS_LOG_G("The thread pool context has been released before : %x", thread_pool_context);
	}
}

int HPNS::Context::ThreadPool::push_thread(int count)
{
	HPNS_LOG_G("Add %d threads to thread pool",count);
	for (size_t i = 0; i < count; i++)
	{
		if (++global_context->current_thread_count > global_context->max_thread_count)
		{
			global_context->current_thread_count--;
			throw HPNS_Exception("Maximum number of threads exceeded  (超出最大綫程數)   %d/%d", global_context->current_thread_count, global_context->max_thread_count);
			return -1;
		}


		std::thread* thread_buff = new std::thread([&]() {
			while (true)
			{
				if (TPCtx.close_thread_count > 0)
				{
					std::lock_guard<std::mutex> lock(TPCtx.thread_pool_mutex);
					TPCtx.close_thread_count--;
					HPNS_LOG_G("pop current thread",0);
					global_context->current_thread_count--;
					TPCtx.remove_list.push_back(TPCtx.thread_list[std::this_thread::get_id()]);
					TPCtx.thread_list.erase(std::this_thread::get_id());
					return;
				}

				std::unique_lock<std::mutex> lock(TPCtx.thread_pool_mutex);
				TPCtx.cv.wait(lock, [&] { return !TPCtx.task_list.empty() || TPCtx.close_thread_count >= 0; });

				if (TPCtx.task_list.empty())
					continue;

				Task task = TPCtx.task_list.front();
				TPCtx.task_list.pop();
				HPNS_LOG_G("Execute task %s",task.command_name.c_str());

				try
				{
					task.network_system->call_command(task.command_name, task.data, task.network_system, task.device);
				}
				catch (const std::exception& err)
				{
					HPNS_LOG_G("thread pool -> task <> error message : %s", task.command_name.c_str(), err.what());
					//printf("\n thread pool -> task <> error message : %s", task.command_name.c_str(), err.what());
				}

			}
			});
		HPNS_LOG_G("push thread to thread pool  -thread : 0x%x", thread_buff);
		TPCtx.thread_list[thread_buff->get_id()] = thread_buff;
	}
	return global_context->current_thread_count;
}

int HPNS::Context::ThreadPool::pop_thread(int count)
{
	TPCtx.close_thread_count += count;
	TPCtx.cv.notify_one();
	if (TPCtx.close_thread_count > TPCtx.thread_list.size())
	{
		TPCtx.close_thread_count = 0;
		throw HPNS_Exception("Exceeded the number of popable threads ");
	}

	HPNS_LOG_G("Request to pop %d threads", count);
	return TPCtx.close_thread_count;
}

void HPNS::Context::ThreadPool::join_all_thread()
{
	HPNS_LOG_G("join thread pool all thread",0);
	for (auto thread : TPCtx.thread_list) {
		thread.second->join();
	}
}

void HPNS::Context::ThreadPool::shutdown()
{
	HPNS_LOG_G("Request that all threads be closed.",0);
	TPCtx.close_thread_count = TPCtx.thread_list.size();
}

void HPNS::Context::ThreadPool::clear_all_task()
{
	HPNS_LOG_G("Clear all tasks.",0);
	TPCtx.thread_pool_mutex.lock();
	while (!TPCtx.task_list.empty())
		TPCtx.task_list.pop();
	TPCtx.thread_pool_mutex.unlock();
}

void HPNS::Context::ThreadPool::push_task(const Task task)
{
	if (task.command_name.empty())
	{
		throw HPNS_Exception("Pushed a null task");
		return;
	}

	HPNS_LOG_G("Push the task.",0);

	TPCtx.task_list.push(task);
	TPCtx.cv.notify_one();

	if (!TPCtx.remove_list.empty())
	{
		HPNS_LOG_G("Clean up empty thread data.", 0);
		for (size_t i = 0; i < TPCtx.remove_list.size(); i++)
		{
			std::thread* remove_thread = TPCtx.remove_list[i];
			if (remove_thread != nullptr)
			{
				delete remove_thread;
				remove_thread = nullptr;
			}
		}
		TPCtx.remove_list.clear();
	}

}

size_t HPNS::Context::ThreadPool::get_current_thread_count()
{
	return TPCtx.thread_list.size();
}

size_t HPNS::Context::ThreadPool::get_task_count()
{
	return TPCtx.task_list.size();
}

