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
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
typedef struct sockaddr HSOCKADDR;
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"Ws2_32.lib")
typedef SOCKADDR HSOCKADDR;
#endif // _WIN32

HPNS::Context::HContext* global_context = nullptr;
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
	if (working)
		return;
	working = true;

	if (thread_activate && working_thread == nullptr)
	{
		HPNS::Context::GetCurrentContext()->current_thread_count++;
		if (global_context->current_thread_count > global_context->max_thread_count)
		{
			throw HPNS_Exception("Maximum number of threads exceeded  (超出最大綫程數)   %d/%d", global_context->current_thread_count, global_context->max_thread_count);
			global_context->current_thread_count--;
			return;
		}
		working_thread = new std::thread([&]() {
			while (working) {
				try
				{
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
		while (working) { Update(); }
}
void HPNS::Internal::Base_NetworkObject::Close()
{
	working = false; if (working_thread != nullptr) { Client_CloseClientConnet(); static_cast<std::thread*>(working_thread)->join(); delete working_thread; working_thread = nullptr; HPNS::Context::GetCurrentContext()->current_thread_count--; }
}
void HPNS::Internal::Base_NetworkObject::push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)> function, bool cover)
{
	if (cover || !command_is_exist(command_name))
		COMMAND_LIST[command_name] = function;
	else
		throw HPNS_Exception("A command with the same name already exists (command name :%s) (已存在同名的命令)", command_name);
}
void HPNS::Internal::Base_NetworkObject::erase_command(const char* command_name)
{
	if (command_is_exist(command_name))
		COMMAND_LIST.erase(command_name);
	else
		throw HPNS_Exception("Requesting to erase a non-existent command  請求刪除不存在的命令  (command name : %s)", command_name);
}
bool HPNS::Internal::Base_NetworkObject::command_is_exist(const char* command_name)
{
	return COMMAND_LIST.find(command_name)!= COMMAND_LIST.end();
}
bool HPNS::Internal::Base_NetworkObject::call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system,HPNS::ConnectDevice device)
{
	if (!global_context->call_command(command_name, data,this, device))
	{
		auto command = COMMAND_LIST.find(command_name);
		if (command != COMMAND_LIST.end())
			command->second(data, this,device);
		else
			return false;
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
		for (auto& command : ToCOMMAND_LIST(network_object->command_list))
			COMMAND_LIST[command.first] = command.second;
	}
	else
	{
		for (auto& command : ToCOMMAND_LIST(network_object->command_list)) {
			if (COMMAND_LIST.find(command.first) == COMMAND_LIST.end())
				COMMAND_LIST[command.first] = command.second;
		}
	}
}
void HPNS::Internal::Base_NetworkObject::clear_command() { COMMAND_LIST.clear(); }

void HPNS::Internal::Base_NetworkObject::init_command_list()
{
	if (command_list == nullptr)
		command_list = new std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)>>;
}
void HPNS::Internal::Base_NetworkObject::release_command_list()
{
	if (command_list!= nullptr)
	{
		delete command_list;
		command_list = nullptr;
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
#else
	int opt=1;
#endif // _WIN32
	struct sockaddr_in addr;
	HPNS::ConnectDevice sListen;
	fd_set  readSet;//定义一个读（接受消息）的集合
	int thread_count_ = 0;
};


#define TCP_IP4_CTX (*static_cast<TCP_IP4_Server_Context*>(network_context))
HPNS::Server::TCP_IP4::TCP_IP4( HPort port, const char* ip, int thread_count)
{
	init_command_list();
	if (network_context == nullptr)
	{
		network_context = new TCP_IP4_Server_Context;
	}

	if (thread_count < 0 && HPNS::Context::GetCurrentContext()->current_thread_count < global_context->max_thread_count)
	{
		global_context->thread_pool.push_thread(global_context->max_thread_count - global_context->current_thread_count);
	}
	else
	{
		global_context->thread_pool.push_thread(thread_count);
		TCP_IP4_CTX.thread_count_ = thread_count;
	}

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &TCP_IP4_CTX.wd) == SOCKET_ERROR)
	{
		throw HPNS_Exception("WSAStartup  error code : %d",GetLastError());
		return;
	}
#endif // _WIN32


	TCP_IP4_CTX.sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (TCP_IP4_CTX.sListen == INVALID_SOCKET)
	{
		throw HPNS_Exception("socket error code : %d", GetLastError());
		return;
	}

#ifndef _WIN32
	setsockopt(TCP_IP4_CTX.sListen, SOL_SOCKET, SO_REUSEADDR, &TCP_IP4_CTX.opt, sizeof(TCP_IP4_CTX.opt));
#endif // _WIN32

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
	TCP_IP4_CTX.addr.sin_addr.s_addr = inet_addr(ip);
#endif // _WIN32
	TCP_IP4_CTX.addr.sin_family = AF_INET;
	TCP_IP4_CTX.addr.sin_port = htons(port);
}


void HPNS::Server::TCP_IP4::Listen()
{
	int len = sizeof(struct sockaddr_in);
	if (bind(TCP_IP4_CTX.sListen, (HSOCKADDR*)&TCP_IP4_CTX.addr, len) == SOCKET_ERROR)
	{
		throw HPNS_Exception("bind  error: %d", GetLastError());
		return;
	}

	if (listen(TCP_IP4_CTX.sListen, 5) == SOCKET_ERROR)
	{
		throw HPNS_Exception("listen  error: %d", GetLastError());
		return;
	}


	FD_ZERO(&TCP_IP4_CTX.readSet);//初始化集合
	FD_SET(TCP_IP4_CTX.sListen, &TCP_IP4_CTX.readSet);
}

HPNS::Server::TCP_IP4::~TCP_IP4()
{
	if (working){Close();}
	release_command_list();
	closesocket(TCP_IP4_CTX.sListen);
	global_context->thread_pool.pop_thread(TCP_IP4_CTX.thread_count_);
	if (network_context != nullptr)
	{
		delete network_context;
		network_context = nullptr;
	}
	if (message_buffer!=nullptr)
	{
		delete message_buffer;
		message_buffer = nullptr;
	}
}

void HPNS::Server::TCP_IP4::Update()
{
	fd_set tmpSet;
	FD_ZERO(&tmpSet);
	FD_SET(0, &tmpSet);
	tmpSet = TCP_IP4_CTX.readSet;
	struct timeval timeout; timeout.tv_sec = 2; // 5秒超時 timeout.tv_usec = 0


	int ret = select(1, &tmpSet, NULL, NULL, &timeout);

	if (ret == SOCKET_ERROR)
	{
		//throw HPNS_Exception("socket error :%d", WSAGetLastError());
		return;
	}


	for (size_t i = 0; i < tmpSet.fd_count; i++)
	{
		HPNS::ConnectDevice client_connet = tmpSet.fd_array[i];

		if (client_connet == TCP_IP4_CTX.sListen)
		{
			HPNS::ConnectDevice client = accept(client_connet, NULL, NULL);
			if (TCP_IP4_CTX.readSet.fd_count < FD_SETSIZE)
			{
				FD_SET(client, &TCP_IP4_CTX.readSet);
				if(callbacks.ClientEntry)
					callbacks.ClientEntry(client_connet, this);
			}
			else
			{
				if(callbacks.ServerCapacityExceeded)
					callbacks.ServerCapacityExceeded(client_connet, this);

				throw HPNS_Exception("The number of clients is greater than the server client capacity (客戶端數量大於伺服器客戶端容量)");
			}
		}
		else
		{
			std::vector<char> buffer(HPNS_RECV_BUFFER_SIZE);
			ret = recv(client_connet, buffer.data(), HPNS_RECV_BUFFER_SIZE, 0);
			if (ret == SOCKET_ERROR || ret <=0)
			{
				if(callbacks.ClientLeaves)
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
						callbacks.ReceiveMessageDecryption_Ex(buffer,client_connet,this);
					
					nlohmann::json message = nlohmann::json::from_msgpack(buffer, ret);
					printf("\n recv message - command : %s", message["cmd"].get<std::string>().c_str());
					global_context->thread_pool.push_task(message["cmd"].get<std::string>(), message["data"], this, client_connet);
				}
				catch (const nlohmann::json::exception& err)
				{
					throw HPNS_Exception("recv message json unpack error : %s",err.what());
				}

			}
		}
	}
}


bool HPNS::Server::TCP_IP4::MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data)
{
	nlohmann::json message;
	std::vector<char> msg_buffer;
	try
	{
		if(!data.is_null())
			message["data"] = data;
		message["cmd"] = command_name;
		nlohmann::json::to_msgpack(message, msg_buffer);
		message.clear();
	}
	catch (const nlohmann::json::exception& err)
	{
		throw HPNS_Exception("SendMessageToClient Json error : %s",err.what());
		return false;
	}
	if (callbacks.SendMessageEncrypted)
		callbacks.SendMessageEncrypted(msg_buffer);
	if (callbacks.SendMessageEncrypted_Ex)
		callbacks.SendMessageEncrypted_Ex(msg_buffer,device,this);
	return send(device, msg_buffer.data(), msg_buffer.size(), 0)>0;
}

const char* HPNS::Server::TCP_IP4::MSG_GetDeviceIP(HPNS::ConnectDevice device)
{
	sockaddr_in localAddr;
	int addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == SOCKET_ERROR) {
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

bool HPNS::Server::TCP_IP4::MSG_CloseClientConnet(HPNS::ConnectDevice device)
{
	if (callbacks.ClientLeaves)
		callbacks.ClientLeaves(device, this);
	closesocket(device);
	FD_CLR(device, &TCP_IP4_CTX.readSet);
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
	if (network_context == nullptr)
		network_context = new TCP_IP4_Client_Context;

	if (thread_count <0 && HPNS::Context::GetCurrentContext()->current_thread_count < global_context->max_thread_count)
	{
		global_context->thread_pool.push_thread(global_context->max_thread_count - global_context->current_thread_count);
	}
	else
	{
		global_context->thread_pool.push_thread(thread_count);
		TCP_IP4_CTX.thread_count_ = thread_count;
	}

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &TCP_IP4_CTX_CLI.wd) != 0)
	{
		throw HPNS_Exception("WSAStartup  error code : %d", GetLastError());
		return ;
	}
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




}

HPNS::Client::TCP_IP4::~TCP_IP4()
{
	if (working) { Close(); }
	release_command_list();
	closesocket(TCP_IP4_CTX_CLI.Server);
	global_context->thread_pool.pop_thread(TCP_IP4_CTX_CLI.thread_count_);
	if (network_context != nullptr)
	{
		delete network_context;
		network_context = nullptr;
	}
	if (message_buffer != nullptr)
	{
		delete message_buffer;
		message_buffer = nullptr;
	}
}

bool HPNS::Client::TCP_IP4::Connect()
{
	TCP_IP4_CTX_CLI.Server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (TCP_IP4_CTX_CLI.Server == INVALID_SOCKET)
	{
		throw HPNS_Exception("socket  error code : %d", GetLastError());
		return false;
	}

	int len = sizeof(struct sockaddr_in);
	if (connect(TCP_IP4_CTX_CLI.Server, (SOCKADDR*)&TCP_IP4_CTX_CLI.addr, len) == SOCKET_ERROR)
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
	if (ret == 0 || ret == SOCKET_ERROR)
	{
		//Client_CloseClientConnet();
		return;
	}

	try
	{
		buffer.resize(ret);
		if (callbacks.ReceiveMessageDecryption)
			callbacks.ReceiveMessageDecryption(buffer);
		if (callbacks.ReceiveMessageDecryption_Ex)
			callbacks.ReceiveMessageDecryption_Ex(buffer, TCP_IP4_CTX_CLI.Server,this);

		nlohmann::json message = nlohmann::json::from_msgpack(buffer, ret);
		global_context->thread_pool.push_task(message["cmd"].get<std::string>(), message["data"], this, TCP_IP4_CTX_CLI.Server);
	}
	catch (const nlohmann::json::exception&err)
	{
		throw HPNS_Exception("recv message json error : %s",err.what());
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
		callbacks.SendMessageEncrypted_Ex(msg_buffer,device,this);
	return send(device, msg_buffer.data(), msg_buffer.size(), 0) > 0;
}

const char* HPNS::Client::TCP_IP4::MSG_GetDeviceIP(HPNS::ConnectDevice device)
{
	sockaddr_in localAddr;
	int addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == SOCKET_ERROR) {
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
	int addrLen = sizeof(localAddr);
	if (getsockname(device, (HSOCKADDR*)&localAddr, &addrLen) == SOCKET_ERROR) {
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
	return MSG_SendMessageToClient(TCP_IP4_CTX_CLI.Server,command_name,data);
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
	return global_context;
}

void HPNS::Context::SetCurrentContext(HContext* new_context)
{
	if(new_context==nullptr)
		throw HPNS_Exception("\"new_context\" is empty (\"new_context\" 爲空)");
	if (global_context != nullptr)
		ReleaseContext();
	global_context = new_context;
}

void HPNS::Context::ReleaseContext()
{
	if (global_context != nullptr)
	{
		delete global_context;
		global_context = nullptr;
	}
}

void HPNS::Context::ReleaseContext(HContext* target_context)
{
	if (target_context != nullptr)
	{
		delete target_context;
		target_context = nullptr;
	}
}



//-----------------------------------------------------------------------------------------
// HContext class

void HPNS::Context::HContext::init_command_list()
{
	if (command_list == nullptr)
		command_list = new std::unordered_map<std::string, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)>>;
}

void HPNS::Context::HContext::release_command_list()
{
	if (command_list != nullptr)
	{
		delete command_list;
		command_list = nullptr;
	}
}

HPNS::Context::HContext::HContext()
{
	init_command_list();
	max_thread_count = std::thread::hardware_concurrency();
}

void HPNS::Context::HContext::push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)> function, bool cover)
{
	init_command_list();

	if(cover||!command_is_exist(command_name))
		COMMAND_LIST[command_name]= function;
	else
		throw HPNS_Exception("A command with the same name already exists (command name :%s) (已存在同名的命令)",command_name);
}

void HPNS::Context::HContext::erase_command(const char* command_name)
{
	if (command_is_exist(command_name))
		COMMAND_LIST.erase(command_name);
	else
		throw HPNS_Exception("Requesting to erase a non-existent command  請求刪除不存在的命令  (command name : %s)", command_name);
}

bool HPNS::Context::HContext::command_is_exist(const char* command_name)
{
	return COMMAND_LIST.find(command_name) != COMMAND_LIST.end();
}

bool HPNS::Context::HContext::call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system,HPNS::ConnectDevice device)
{
	auto command = COMMAND_LIST.find(command_name);
	if (command != COMMAND_LIST.end())
		command->second(data, network_system,device);
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
		for (auto & command : ToCOMMAND_LIST(context->command_list))
			COMMAND_LIST[command.first] = command.second;
	}
	else
	{
		for (auto& command : ToCOMMAND_LIST(context->command_list)){
			if (COMMAND_LIST.find(command.first) == COMMAND_LIST.end())
				COMMAND_LIST[command.first] = command.second;
		}
	}
}

void HPNS::Context::HContext::clear_command(){COMMAND_LIST.clear();}

std::vector<std::string> HPNS::Context::HContext::get_all_command()
{
	std::vector<std::string> commands;
	for (auto& cmd : COMMAND_LIST){
		commands.push_back(cmd.first);
	}
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
	if(thread_pool_context == nullptr)
		thread_pool_context = new ThreadPoolContext;
}

HPNS::Context::ThreadPool::~ThreadPool()
{
	if (thread_pool_context != nullptr)
	{
		delete thread_pool_context;
		thread_pool_context = nullptr;
	}
}

int HPNS::Context::ThreadPool::push_thread(int count)
{
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
					//thread_count--;
					global_context->current_thread_count--;
					TPCtx.remove_list.push_back( TPCtx.thread_list[std::this_thread::get_id()]);
					TPCtx.thread_list.erase(std::this_thread::get_id());
					return;
				}

				std::unique_lock<std::mutex> lock(TPCtx.thread_pool_mutex);
				TPCtx.cv.wait(lock, [&] { return !TPCtx.task_list.empty() || TPCtx.close_thread_count >= 0; });

				if (TPCtx.task_list.empty())
					continue;

				Task task = TPCtx.task_list.front();
				TPCtx.task_list.pop();
				

				try
				{
					task.network_system->call_command(task.command_name, task.data, task.network_system, task.device);
				}
				catch (const std::exception&err)
				{
					printf("\n thread pool -> task <> error message : %s", task.command_name.c_str(), err.what());
				}

			}
		});

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
		throw HPNS_Exception("Exceeded the number of popable threads " );
	}
	return TPCtx.close_thread_count;
}

void HPNS::Context::ThreadPool::join_all_thread()
{
	for (auto thread : TPCtx.thread_list)
		thread.second->join();
}

void HPNS::Context::ThreadPool::shutdown()
{
	TPCtx.close_thread_count = TPCtx.thread_list.size();
}

void HPNS::Context::ThreadPool::clear_all_task()
{
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

	TPCtx.task_list.push(task);
	TPCtx.cv.notify_one();

	if (!TPCtx.remove_list.empty())
	{
		for (size_t i = 0; i < TPCtx.remove_list.size();i++)
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
