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
#pragma once
#ifndef HALF_PEOPLE_NETWORK_SYSTEM__H__
#define HALF_PEOPLE_NETWORK_SYSTEM__H__


//If it cannot run on your server, please update or download Visual C++ Redistributable 
//如果在你的服務器上跑不了請更新或下載  Visual C++ Redistributable 
//https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170


//-----------------------------------------------------------------------------------------
// Setting
//-----------------------------------------------------------------------------------------

#define HPNS_SERVER_ACTIVATE true
#define HPNS_CLIENT_ACTIVATE true
#define HPNS_RECV_BUFFER_SIZE 4096


#if HPNS_SERVER_ACTIVATE || HPNS_CLIENT_ACTIVATE
#include <exception>
#include <functional>
#include <nlohmann/json.hpp>


//	Register command demo :
//
//	HPNS_REG_COMMAND(command_name, HPNS_COMMAND_HEAD
//	{
//	
//	
//	
//	})





namespace HPNS
{
#ifdef _WIN32
	typedef unsigned __int64 ConnectDevice;
	typedef unsigned short HPort;
#endif // _WIN32
}


namespace HPNS::Internal
{
	class Base_NetworkObject;
	struct CallbackIO
	{
		std::function <void(ConnectDevice, Base_NetworkObject*)> ClientLeaves;
		std::function <void(ConnectDevice, Base_NetworkObject*)> ClientEntry;
		std::function <void(ConnectDevice, Base_NetworkObject*)> ServerCapacityExceeded;
		std::function <void(std::vector<char>&)> SendMessageEncrypted;
		std::function <void(std::vector<char>&)> ReceiveMessageDecryption;

		std::function <void(std::vector<char>&, ConnectDevice, Base_NetworkObject*)> SendMessageEncrypted_Ex;
		std::function <void(std::vector<char>&, ConnectDevice, Base_NetworkObject*)> ReceiveMessageDecryption_Ex;

		std::function <void(std::string,Base_NetworkObject*)> NetworkSystemThreadExistError;
	};

	class Base_NetworkObject
	{
	public:
		//-----------------------------------------------------------------------------------------
		// main

		virtual void Start(bool thread_activate = false);
		virtual void Close();
		inline bool IsWorking() { return working; }

		inline CallbackIO& SetupCallback() { return	callbacks; }

		virtual void Update() {};
		//-----------------------------------------------------------------------------------------
		//command

		virtual void push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*, HPNS::ConnectDevice)> function, bool cover = false);
		virtual void erase_command(const char* command_name);
		virtual bool command_is_exist(const char* command_name);
		virtual bool call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device);
		virtual void insert_commands(Base_NetworkObject* network_object, bool cover = false);
		virtual void clear_command();
		//-----------------------------------------------------------------------------------------
		//message - server
		virtual bool MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data = nlohmann::json()) { return false; }
		virtual const char* MSG_GetDeviceIP(HPNS::ConnectDevice device) { return "null"; }
		virtual bool MSG_CloseClientConnet(HPNS::ConnectDevice device) { return false; }
		virtual bool MSG_IsConnected(HPNS::ConnectDevice device) { return false; }
		//message - client
#if HPNS_CLIENT_ACTIVATE
		virtual bool Client_SendMessageToServer(const char* command_name, nlohmann::json data = nlohmann::json()) { return false; }
		virtual const char* Client_GetDeviceIP() { return "null"; }
		virtual bool Client_CloseClientConnet() { return false; }
		virtual bool Client_IsConnected() { return false; }
#endif // HPNS_CLIENT_ACTIVATE
		void* message_buffer = nullptr;//Reserved for users to save information (will be automatically deleted)
	protected:
		void* network_context = nullptr;
		void* command_list = nullptr;
		void* working_thread = nullptr;
		bool working = false;
		CallbackIO callbacks;
		void init_command_list();
		void release_command_list();
	};
}

//-------------------------------------------------------------------------------------------
// Server
//-------------------------------------------------------------------------------------------
#if HPNS_SERVER_ACTIVATE
namespace HPNS::Server
{

	class TCP_IP4 :public HPNS::Internal::Base_NetworkObject
	{
	public:
		TCP_IP4( HPort port, const char* ip = "127.0.0.1",int thread_count = -1);
		~TCP_IP4();
		virtual void Update() override;
		void Listen();
		virtual bool MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data = nlohmann::json())override;
		virtual const char* MSG_GetDeviceIP(HPNS::ConnectDevice device)override;
		virtual bool MSG_CloseClientConnet(HPNS::ConnectDevice device)override;
	private:

	};

}
#endif // HPNS_SERVER_ACTIVATE
//-------------------------------------------------------------------------------------------
// Client
//-------------------------------------------------------------------------------------------
#if HPNS_CLIENT_ACTIVATE
namespace HPNS::Client
{
	class TCP_IP4 :public HPNS::Internal::Base_NetworkObject
	{
	public:
		TCP_IP4(const char* ip ,HPort port, int thread_count = 2);
		~TCP_IP4();
		virtual void Update() override;
		bool Connect();

		virtual bool MSG_SendMessageToClient(HPNS::ConnectDevice device, const char* command_name, nlohmann::json data = nlohmann::json())override;
		virtual const char* MSG_GetDeviceIP(HPNS::ConnectDevice device)override;
		virtual bool MSG_CloseClientConnet(HPNS::ConnectDevice device)override;
		virtual bool Client_IsConnected() override;

		virtual bool Client_SendMessageToServer(const char* command_name, nlohmann::json data = nlohmann::json())override;
		virtual const char* Client_GetDeviceIP()override;
		virtual bool Client_CloseClientConnet()override;
		virtual bool MSG_IsConnected(HPNS::ConnectDevice device) override;
	private:

	};
}
#endif // HPNS_CLIENT_ACTIVATE
//-------------------------------------------------------------------------------------------
// Global
//-------------------------------------------------------------------------------------------
namespace HPNS::Context
{
	struct Task
	{
		Task(std::string cmd, nlohmann::json dt,HPNS::Internal::Base_NetworkObject* ns, HPNS::ConnectDevice dev)
		{
			command_name = cmd;
			network_system = ns;
			device = dev;
			data = dt;
		}
		std::string command_name;
		HPNS::Internal::Base_NetworkObject* network_system = nullptr;
		HPNS::ConnectDevice device;
		nlohmann::json data;
	};

	class ThreadPool
	{
	public:
		ThreadPool();
		~ThreadPool();
		//thread count control
		int push_thread(int count);
		int pop_thread(int count);
		//threads function

		void join_all_thread();
		void shutdown();
		//Task
		void clear_all_task();
		void push_task(const Task task);
		inline void push_task(std::string command_name,nlohmann::json data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device) {push_task(Task(command_name,data, network_system, device));}
		//info
		size_t get_current_thread_count();
		size_t get_task_count();
	private:
		void* thread_pool_context = nullptr;
	};


	//context class
	class HContext
	{
	public:
		HContext();
		~HContext() { release_command_list(); }
		//------------command--
		virtual void push_command(const char* command_name, std::function<void(nlohmann::json&, HPNS::Internal::Base_NetworkObject*,HPNS::ConnectDevice)> function,bool cover= false);
		virtual void erase_command(const char* command_name);
		virtual bool command_is_exist(const char* command_name);
		virtual bool call_command(std::string command_name, nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system,HPNS::ConnectDevice device);
		virtual void insert_commands(HContext* context,bool cover = false);
		virtual void clear_command();
		std::vector<std::string> get_all_command();

		size_t current_thread_count = 0;
		size_t max_thread_count = 0;
		ThreadPool thread_pool;
	private:
		void* command_list = nullptr;

		void init_command_list();
		void release_command_list();
	};
	//---- context - function
	HContext* CreateContext();
	HContext* GetCurrentContext();
	void SetCurrentContext(HContext* new_context);
	void ReleaseContext();
	void ReleaseContext(HContext* target_context);
}


#define HPNS_COMMAND_HEAD [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system,HPNS::ConnectDevice device)
#define HPNS_REG_COMMAND(CommandName,Function)\
class REG_CommandRegisterClass__##CommandName {\
public:\
	REG_CommandRegisterClass__##CommandName(){HPNS::Context::GetCurrentContext()->push_command(#CommandName,Function); }\
};\
static REG_CommandRegisterClass__##CommandName REG_COMMAND_VAR____COMMAND_##CommandName;
#endif // 0
#endif // !HALF_PEOPLE_NETWORK_SYSTEM