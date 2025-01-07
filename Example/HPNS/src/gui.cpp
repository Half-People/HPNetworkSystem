#include "gui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "HPNS.h"
#ifdef _WIN32
#include <Windows.h>//MessageBox
#else
#include <iostream>
#endif // _WIN32


#ifdef _WIN32
#define HBeep() Beep(1000, 100);Beep(500, 100);Beep(1000, 100);
#else
#define HBeep() std::cout << "\a";
#endif


void ThreadWidget(const char* label, ImColor col, const ImVec2& size_arg = ImVec2(-1,25))
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return ;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return ;


	// Render
	ImGui::RenderNavCursor(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);


	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, ImVec2(0,0.5), &bb);

	return ;
}

bool show_model = false;
std::string model_message;

HPNS_REG_COMMAND(model, [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device) {
	show_model = true;
	model_message = data.get<std::string>();
})


HPNS_REG_COMMAND(MessageBox, [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device) {
	HMessageBox( "MessageBox",data.get<std::string>().c_str());
})

HPNS_REG_COMMAND(Beep, [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device) {
	HBeep();
})

std::string server_ip="127.0.0.1";
std::string cserver_ip = "127.0.0.1";
std::string ser_message_sender_msg;//2048
std::string ser_message_sender_cmd;//1024
std::string cli_message_sender_msg;//2048
std::string cli_message_sender_cmd;//1024
HPNS::Context::HContext* context = nullptr;
HPNS::Internal::CallbackIO Callback_base;
struct log_item
{
	HPNS::Internal::Base_NetworkObject* network = nullptr;
	bool is_server = false;
	std::string message;
};
struct server_users
{
	struct user
	{
		HPNS::ConnectDevice socket;
		std::string ip;
	};
	std::vector<user> users;
	user* select_user = nullptr;
};
std::vector<log_item> message_log;

void push_log(std::string message,bool is_server,HPNS::Internal::Base_NetworkObject* ns)
{
	log_item it;
	it.is_server = is_server;
	it.network = ns;
	it.message = message;
	if (message_log.size() > 50)
		message_log.erase(message_log.begin());
	message_log.push_back(it);
}

void GUI::init()
{
	context = HPNS::Context::CreateContext();
	context->Subsystem_LogMessage_callback = [](std::string msg) {
		push_log(msg, false, nullptr);
	};
	context->thread_pool.push_thread(1);
	server_ip.resize(1024);
	cserver_ip.resize(1024);
	ser_message_sender_msg.resize(2048);
	ser_message_sender_cmd.resize(1024);
	cli_message_sender_msg.resize(2048);
	cli_message_sender_cmd.resize(1024);

	Callback_base.ClientEntry = [](HPNS::ConnectDevice socket, HPNS::Internal::Base_NetworkObject* obj)
	{
		
		log_item it;
		if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
		{it.is_server = true;}
		it.network = obj;
		try
		{
			it.message = std::string(it.is_server?"Server [" : "Client [").append(obj->MSG_GetDeviceIP(socket)).append("] Entry");
		}
		catch (const std::exception&err)
		{
			push_log(err.what(), it.is_server, obj);
		}


		if (message_log.size() > 50)
			message_log.erase(message_log.begin());
		message_log.push_back(it);
		server_users::user user;
		user.ip = obj->MSG_GetDeviceIP(socket);
		user.socket = socket;
		static_cast<server_users*>(it.network->message_buffer)->users.push_back(user);
	};

	Callback_base.ClientLeaves = [](HPNS::ConnectDevice socket, HPNS::Internal::Base_NetworkObject* obj)
		{
			log_item it;
			if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
			{
				it.is_server = true;
			}
			it.network = obj;
			try
			{
				it.message = std::string(it.is_server ? "Server [" : "Client [").append(obj->MSG_GetDeviceIP(socket)).append("] Leaves");
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), it.is_server, obj);
			}


			if (message_log.size() > 50)
				message_log.erase(message_log.begin());
			message_log.push_back(it);

			auto msg_buff = static_cast<server_users*>(it.network->message_buffer);

			for (size_t i = 0; i < msg_buff->users.size(); i++)
			{
				if (msg_buff->users[i].socket == socket)
				{
					if (msg_buff->select_user == &msg_buff->users[i])
					{
						msg_buff->select_user = nullptr;
					}
					msg_buff->users.erase(msg_buff->users.begin() + i);
				}
			}
	};

	Callback_base.ServerCapacityExceeded = [](HPNS::ConnectDevice socket, HPNS::Internal::Base_NetworkObject* obj)
		{
			log_item it;
			if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
			{
				it.is_server = true;
			}
			it.network = obj;
			try
			{
				it.message = std::string(it.is_server ? "Server [" : "Client [").append(obj->MSG_GetDeviceIP(socket)).append("] ServerCapacityExceeded!!");
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), it.is_server, obj);
			}

			if (message_log.size() > 50)
				message_log.erase(message_log.begin());
			message_log.push_back(it);
		};

	Callback_base.ReceiveMessageDecryption_Ex = [](std::vector<char>& msg, HPNS::ConnectDevice socket, HPNS::Internal::Base_NetworkObject* obj)
	{
		log_item it;
		if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
			it.is_server = true;
		it.network = obj;
		std::string out_msg;
		try
		{
			nlohmann::json message = nlohmann::json::from_msgpack(msg, msg.size());
			out_msg = message.dump(4);
		}
		catch (const nlohmann::json::exception&err)
		{
			out_msg = err.what();
		}

		try
		{
			it.message = std::string(it.is_server ? "Server [" : "Client [").append(obj->MSG_GetDeviceIP(socket)).append("] ReceiveMessage :").append(out_msg);
		}
		catch (const std::exception&err)
		{
			push_log(err.what(), it.is_server, obj);
		}

		if (message_log.size() > 50)
			message_log.erase(message_log.begin());
		message_log.push_back(it);
	};

	Callback_base.SendMessageEncrypted_Ex = [](std::vector<char>& msg, HPNS::ConnectDevice socket, HPNS::Internal::Base_NetworkObject* obj)
		{
			log_item it;
			if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
				it.is_server = true;
			it.network = obj;
			std::string out_msg;
			try
			{
				nlohmann::json message = nlohmann::json::from_msgpack(msg, msg.size());
				out_msg = message.dump(4);
			}
			catch (const nlohmann::json::exception& err)
			{
				out_msg = err.what();
			}

			try
			{
				it.message = std::string(it.is_server ? "Server [" : "Client [").append(obj->MSG_GetDeviceIP(socket)).append("] SendMessage :").append(out_msg);
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), it.is_server, obj);
			}

			if (message_log.size() > 50)
				message_log.erase(message_log.begin());
			message_log.push_back(it);
		};
	
	Callback_base.NetworkSystemThreadExistError = [](std::string msg, HPNS::Internal::Base_NetworkObject* obj)
	{
		bool server = false;
		if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
			server = true;
		push_log(msg, server, obj);
	};

	Callback_base.LogMessage = [](std::string msg,HPNS::Internal::Base_NetworkObject* obj) {
		bool server = false;
		if (dynamic_cast<HPNS::Server::TCP_IP4*>(obj))
		{
			server = true;
		}
		push_log(msg, server, obj);
	};
}
struct server_item
{
	HPNS::Server::TCP_IP4* server = nullptr;
	std::string ip;
	int port;
};
struct client_item
{
	HPNS::Client::TCP_IP4* client = nullptr;
	std::string ip;
	int port;
};
std::vector<client_item> clients;
std::vector<server_item> servers;

HPNS::Server::TCP_IP4* select_server = nullptr;
HPNS::Client::TCP_IP4* select_client = nullptr;
void draw_show_updatamessage()
{
	static bool IsUpdata = false;
	ImGui::Checkbox("ShowUpdataMessage", &IsUpdata);
	for (auto& a : clients){
		a.client->SetupCallback().ShowUpdataLog = IsUpdata;
	}
	for (auto& a : servers) {
		a.server->SetupCallback().ShowUpdataLog = IsUpdata;
	}
}
void MessageLog()
{
	if (ImGui::Begin("Message Log - Server"))
	{
		draw_show_updatamessage();
		if (ImGui::BeginListBox("###msglog-ser", ImVec2(-1, -1)))
		{
			for (auto& msg : message_log)
			{
				if(msg.is_server&& msg.network != nullptr)
					if (ImGui::Selectable(std::string(msg.message).append("###").append(std::to_string((long long) & msg)).c_str()))
					{
						select_server = static_cast<HPNS::Server::TCP_IP4*>(msg.network);
					}
			}
		}
		ImGui::EndListBox();

	}ImGui::End();

	if (ImGui::Begin("Message Log - Client"))
	{
		draw_show_updatamessage();
		if (ImGui::BeginListBox("###msglog-cli", ImVec2(-1, -1)))
		{
			for (auto& msg : message_log)
			{
				if(!msg.is_server&& msg.network != nullptr)
					if (ImGui::Selectable(std::string(msg.message).append("###").append(std::to_string((long long)&msg)).c_str()))
					{
						select_client = static_cast<HPNS::Client::TCP_IP4*>(msg.network);
					}
			}
		}
		ImGui::EndListBox();
	}ImGui::End();

	if (ImGui::Begin("Message Log"))
	{
		draw_show_updatamessage();
		if (ImGui::BeginListBox("###msglog",ImVec2(-1,-1)))
		{
			for (auto& msg : message_log)
			{
				ImGui::Selectable(std::string(msg.message).append("###").append(std::to_string((long long)&msg)).c_str());
			}
		}
		ImGui::EndListBox();
	}ImGui::End();
}

void MessageSender(bool server)
{
	if (ImGui::BeginChild("message sender",ImVec2(),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders))
	{
		if (server)
		{
			if (select_server ==nullptr)
			{
				ImGui::Text("Server Message Sender : ");
				ImGui::Text("Pls Select Server");
				ImGui::EndChild();
				return;
			}
			auto serverUsers = static_cast<server_users*>(select_server->message_buffer);
			ImGui::Text("Server Message Sender : ");
			ImGui::InputText("command ", ser_message_sender_cmd.data(), 1024);
			ImGui::InputTextMultiline("message", ser_message_sender_msg.data(), 2048);
			
			if (ImGui::Button("SendMessageToClient"))
			{
				if (serverUsers->select_user !=nullptr)
					select_server->MSG_SendMessageToClient(serverUsers->select_user->socket, ser_message_sender_cmd.c_str(), ser_message_sender_msg.c_str());
			}
			ImGui::SameLine();
			if (ImGui::BeginCombo("select client", serverUsers->select_user == nullptr?"null" : std::string(serverUsers->select_user->ip).append(" - ").append(std::to_string(serverUsers->select_user->socket)).c_str()))
			{
				for (auto& user : serverUsers->users)
				{
					if (ImGui::Selectable(std::string(user.ip).append(" -  ").append(std::to_string(user.socket)).c_str(),&user == serverUsers->select_user))
					{
						serverUsers->select_user = &user;
					}
				}
				ImGui::EndCombo();
			}

			if (ImGui::Button("Send Message To All Client"))
			{
				for (auto user : serverUsers->users)
				{
					select_server->MSG_SendMessageToClient(user.socket, ser_message_sender_cmd.c_str(), ser_message_sender_msg.c_str());
				}
			}
		}
		else
		{
			ImGui::Text("Client Message Sender : ");
			if (select_client == nullptr)
			{
				ImGui::Text("pls Select client");
				ImGui::EndChild();
				return;
			}
			ImGui::InputText("command ", cli_message_sender_cmd.data(), 1024);
			ImGui::InputTextMultiline("message", cli_message_sender_msg.data(), 2048);

			if (ImGui::Button("SendMessageToServer"))
			{
				select_client->Client_SendMessageToServer( cli_message_sender_cmd.c_str(), cli_message_sender_msg.c_str());
			}
		}

	}ImGui::EndChild();
}

void SelectServer(float size)
{
	if (ImGui::BeginChild("select server",ImVec2(size-50,0),ImGuiChildFlags_Borders))
	{
		if (select_server == nullptr)
		{
			ImGui::Text("not have select server");
			ImGui::EndChild();
			return;
		}
		try
		{
			if (ImGui::Button("Listen"))
			{
				select_server->Listen();
			}ImGui::SameLine();
			if (ImGui::Button("Start"))
			{
				select_server->Start(true);
			}ImGui::SameLine();
			if (ImGui::Button("Close"))
			{
				select_server->Close();
				for (size_t i = 0; i < servers.size(); i++)
				{
					if (servers[i].server == select_server) {
						delete select_server;
						select_server = nullptr;
						servers.erase(servers.begin() + i);
					}
				}
			}


			if (select_server != nullptr)
				ImGui::Text("Server is working : %s", select_server->IsWorking() ? "true" : "false");
		}
		catch (const std::exception&err)
		{
			push_log(err.what(), true, select_server);
		}


		MessageSender(true);
	}ImGui::EndChild();
}

void ServerBuild(float half_size)
{
	if (ImGui::BeginChild("Server Build",ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders))
	{
		static int port = 8000;
		static int thread_index = 0;
		ImGui::Text("Server Build :\n");

		ImGui::InputText("ServerIP", server_ip.data(), 1024);
		ImGui::SetNextItemWidth(half_size * 0.25);
		ImGui::DragInt("Port", &port, 0.5, 1024, 10000);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(half_size * 0.25);
		ImGui::DragInt("Thread", &thread_index, 0.5, 0, 25);
		if (ImGui::Button("add server"))
		{
			bool this_port_have_server = false;
			for (auto& ser : servers)
			{
				if (ser.port == port)
				{
					if (ser.ip == server_ip.c_str())
					{
						this_port_have_server = true;
						push_log(std::string("add server -> server ip-port :   ").append(server_ip.c_str()).append(":").append(std::to_string(port)).append(" The same server already exists.").c_str(), true, ser.server);
					}
				}
			}

			if (this_port_have_server)
			{
				ImGui::EndChild(); return;
			}

			try
			{
				auto server_buff = new HPNS::Server::TCP_IP4(port, server_ip.c_str(),thread_index);
				server_buff->SetupCallback() = Callback_base;
				server_buff->message_buffer = new server_users;
			
				servers.push_back({ server_buff ,server_ip .c_str(),port});
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), true, nullptr);
			}
		}
	}ImGui::EndChild();
	SelectServer(half_size);
}

void ServerList(float list_size)
{
	for (size_t i = 0; i < servers.size(); i++)
	{
		server_item& it = servers[i];
		if (ImGui::Selectable(std::string("server:").append(it.ip).append(":").append(std::to_string(it.port)).c_str(),it.server == select_server,0,ImVec2(list_size-50,0)))
		{
			select_server = it.server;
		}
		ImGui::SameLine();
		if (ImGui::Button(std::string("x###").append(std::to_string((long long)it.server)).c_str()))
		{
			try
			{
				it.server->Close();
				if (it.server == select_server)
					select_server = nullptr;
				delete it.server;
				servers.erase(servers.begin() + i);
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), true, nullptr);
			}

		}
	}
}

void ClientList(float size)
{
	for (size_t i = 0; i < clients.size(); i++)
	{
		client_item& it = clients[i];
		if (ImGui::Selectable(std::string("clienr connect server:").append(it.ip).append(":").append(std::to_string(it.port)).append("###").append(std::to_string((long long) & it)).c_str(), it.client == select_client, 0, ImVec2(size - 50, 0)))
		{
			select_client = it.client;
		}
		ImGui::SameLine();
		if (ImGui::Button(std::string("x###").append(std::to_string((long long)it.client)).c_str()))
		{
			try
			{
				it.client->Close();
				if (it.client == select_client)
					select_client = nullptr;
				delete it.client;
				clients.erase(clients.begin() + i);
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), false, it.client);
			}
		}
	}
}

void SelectClient(float size)
{
	if (ImGui::BeginChild("select client", ImVec2(size - 50, 0), ImGuiChildFlags_Borders))
	{
		if (select_client == nullptr)
		{
			ImGui::Text("not have select client");
			ImGui::EndChild();
			return;
		}

		try
		{
			if (ImGui::Button("Connect"))
			{
				select_client->Connect();
			}ImGui::SameLine();
			if (ImGui::Button("Start"))
			{
				select_client->Start(true);
			}ImGui::SameLine();
			if (ImGui::Button("Close"))
			{
				select_client->Close();
				for (size_t i = 0; i < clients.size(); i++)
				{
					if (clients[i].client == select_client) {
						delete select_client;
						select_client = nullptr;
						clients.erase(clients.begin() + i);
					}
				}
			}ImGui::SameLine();
			if (ImGui::Button("Disable connection"))
			{
				select_client->Client_CloseClientConnet();
			}
			if (select_client != nullptr)
			{
				ImGui::Text("Client is working : %s", select_client->IsWorking() ? "true": "false");
				ImGui::Text("Client is connet server : %s", select_client->Client_IsConnected() ? "true" : "false");
			}

		}
		catch (const std::exception&err)
		{
			push_log(err.what(), false, nullptr);
		}

		MessageSender(false);
	}ImGui::EndChild();
	

}

void ClientBuild(float half_size)
{
	if (ImGui::BeginChild("Client Build", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
	{
		static int port = 8000;
		static int thread_index = 0;
		ImGui::Text("Client Build :\n");

		ImGui::InputText("TargetServerIP", cserver_ip.data(), 1024);
		ImGui::SetNextItemWidth(half_size * 0.25);
		ImGui::DragInt("Port", &port, 0.5, 1024, 10000);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(half_size * 0.25);
		ImGui::DragInt("Thread", &thread_index, 0.5, 0, 25);
		if (ImGui::Button("add client"))
		{
			try
			{
				auto server_buff = new HPNS::Client::TCP_IP4( server_ip.c_str(), port, thread_index);
				server_buff->SetupCallback() = Callback_base;
				server_buff->message_buffer = new server_users;

				clients.push_back({ server_buff,cserver_ip.c_str(),port });
			}
			catch (const std::exception&err)
			{
				push_log(err.what(), false, nullptr);
			}

		}
	}ImGui::EndChild();
	SelectClient(half_size);
}

void ThreadPool()
{
	if (ImGui::Begin("thread pool"))
	{
		if (ImGui::BeginChild("threads view",ImVec2(),ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeY))
		{
			ImGui::ProgressBar(context->current_thread_count/ (float)context->max_thread_count);
			ImGui::Text("Thread pool :%d", context->thread_pool.get_current_thread_count());
			for (size_t i = 0; i < context->thread_pool.get_current_thread_count(); i++)
			{
				ThreadWidget("Thread pool thread",ImColor(245, 132, 66));
			}
			ImGui::Text("NetworkSystem : %d", context->current_thread_count - context->thread_pool.get_current_thread_count());
			for (size_t i = 0; i < context->current_thread_count - context->thread_pool.get_current_thread_count(); i++)
			{
				ThreadWidget("network system thread", ImColor(66, 245, 138));
			}
			ImGui::Text("allocable : %d", context->max_thread_count - context->current_thread_count);
			for (size_t i = 0; i < context->max_thread_count- context->current_thread_count; i++)
			{
				ThreadWidget("idle thread", ImColor(66, 153, 245));
			}
		}ImGui::EndChild();

		if (ImGui::BeginChild("push thread",ImVec2(), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
		{
			ImGui::Text("Push Thread :");
			static int thread_count = 0;
			ImGui::DragInt("thread count### push thread count", &thread_count, 0.1, 0, context->max_thread_count);
			if (ImGui::Button("Push Thread"))
			{
				try
				{
					context->thread_pool.push_thread(thread_count);
				}
				catch (const std::exception& err)
				{
					push_log(err.what(), 0, nullptr);
				}
				
			}
		}ImGui::EndChild();

		if (ImGui::BeginChild("pop thread", ImVec2(), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
		{
			ImGui::Text("Pop Thread :");
			static int thread_count = 0;
			if (context->current_thread_count == 0)
				thread_count = 0;
			ImGui::DragInt("thread count### pop thread count", &thread_count, 0.1, 0, context->current_thread_count);
			if (ImGui::Button("Pop Thread"))
			{
				try
				{
					context->thread_pool.pop_thread(thread_count);
				}
				catch (const std::exception&err)
				{
					push_log(err.what(), 0, nullptr);
				}

			}
		}ImGui::EndChild();

		ImGui::Text("current task count : %d", context->thread_pool.get_task_count());

		if (ImGui::Button("clear all task"))
		{
			context->thread_pool.clear_all_task();
		}

	}ImGui::End();
}

void Context()
{
	if (ImGui::Begin("Context"))
	{
		if (ImGui::BeginChild("Commands View",ImVec2(),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders))
		{
			ImGui::Text("global command list :");
			std::vector<std::string> cmds = context->get_all_command();
			for (std::string& cmd : cmds)
				ThreadWidget(cmd.c_str(), ImColor(9, 125, 96));
		}ImGui::EndChild();

		if (ImGui::BeginChild("Command erase",ImVec2(),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders))
		{
			static char cmd_name[1024];
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 70);
			ImGui::InputText("command", cmd_name, 1024);
			if (ImGui::Button("erase command",ImVec2(-1,0))){
				try
				{
					context->erase_command(std::string(cmd_name).c_str());
				}
				catch (const std::exception&err)
				{
					push_log(err.what(), false, nullptr);
				}

			}
		}ImGui::EndChild();

		if (ImGui::BeginChild("push command", ImVec2(), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
		{
			static char cmd_name[1024];
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 70);
			ImGui::InputText("command", cmd_name, 1024);
			if (ImGui::Button("push command", ImVec2(-1, 0))) {
				try
				{
					context->push_command(std::string(cmd_name).c_str(), HPNS_COMMAND_HEAD{
						data = std::string("this is your push the test command :").append(cmd_name).append("message !!").append("\n your old message is :\n").append(data.get<std::string>());
						network_system->MSG_SendMessageToClient(device, "model", data);
					});
				}
				catch (const std::exception& err)
				{
					push_log(err.what(), false, nullptr);
				}

			}
		}ImGui::EndChild();
		

	}ImGui::End();
}


void about()
{
	if (ImGui::BeginPopupModal("about###aboutmodel",0,ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("The author of this sample project is Half People (2024/12/28)");
		ImGui::Text("Third-party libraries :");
		ImGui::TextLinkOpenURL("ImGui", "https://github.com/ocornut/imgui/tree/docking");
		ImGui::TextLinkOpenURL("SDL2", "https://github.com/libsdl-org/SDL/tree/SDL2");
		ImGui::TextLinkOpenURL("nlohmann/json", "https://github.com/nlohmann/json"); ImGui::SameLine();
		ImGui::Text("(Network framework is required)");
		ImGui::Text("network framework :");
		ImGui::TextLinkOpenURL("HPNetworkSystem", "https://github.com/Half-People/HPNetworkSystem");
		ImGui::Text("community :");
		ImGui::TextLinkOpenURL("Discord ","https://discord.gg/WrAQXKGYCp");
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);

		if (ImGui::Button("close")){
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}


void GUI::updata()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::MenuItem("about"))
		{
			ImGui::OpenPopup("about###aboutmodel");
		}
		about();
		ImGui::EndMainMenuBar();
	}


	if (ImGui::Begin("Server"))
	{
		float win_size = ImGui::GetWindowWidth();
		if (ImGui::BeginChild("server_list",ImVec2(win_size*0.25,0),ImGuiChildFlags_Border|ImGuiChildFlags_ResizeX))
		{
			ServerList(ImGui::GetWindowWidth());
		}ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginGroup();
		ServerBuild(win_size);
		ImGui::EndGroup();
	}ImGui::End();
	if (ImGui::Begin("Client"))
	{
		float win_size = ImGui::GetWindowWidth();
		if (ImGui::BeginChild("client_list",ImVec2(win_size*0.25,0),ImGuiChildFlags_Borders| ImGuiChildFlags_ResizeX))
		{
			ClientList(ImGui::GetWindowWidth());
		}ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginGroup();
		ClientBuild(win_size);
		ImGui::EndGroup();
	}ImGui::End();
	MessageLog();
	ThreadPool();
	Context();

	if (show_model)
	{
		show_model = false;
		ImGui::OpenPopup("Show Popup");
	}
	if (ImGui::BeginPopupModal("Show Popup"))
	{
		ImGui::Text(model_message.c_str());
		if (ImGui::Button("close")){
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

}

void GUI::close()
{
}

