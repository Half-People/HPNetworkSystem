# HPNS - HPNetworkSystem
### This is a highly open network system based on command mode.
### All you need is a few lines of code. Add the required functionality. No need to manage thread/socket yourself...

![System Node Struct ](https://github.com/Half-People/HPNetworkSystem/blob/main/NSBG.png?raw=true)

# Contains subsystems
- [x] TCP/IPV4 Server[^1] / Client
- [ ] TCP/IPV6 
- [ ] UDP/IPV4
- [ ] UDP/IPV6
- [x] ThreadPool
- [x] CommandRegistrar
- [x] Global Context (Can synchronize data)

> [!NOTE]
> Requires dependence on third-party libraries [nlohmann/json](https://github.com/nlohmann/json)

# System structure.
![System Node Struct ](https://github.com/Half-People/HPNetworkSystem/blob/main/HPNS.jpg?raw=true)

# Example      
### [For more in-depth examples or functions, please refer to the Wiki](https://github.com/Half-People/HPNetworkSystem/wiki)
## Server
```cpp
#include <iostream>
#include <HPNS.h>

HPNS_REG_COMMAND(command_name, [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device)
{
		std::cout << "\n Client ["<< network_system->MSG_GetDeviceIP(device) <<"] Send Server Message :" << data.get<std::string>();
		data = "hello client this is server";
		network_system->MSG_SendMessageToClient(device, "server_message", data);
})


void server_runing()
{
	HPNS::Context::CreateContext();
	HPNS::Server::TCP_IP4 Server(8080);
	auto& io = Server.SetupCallback();
	io.ClientEntry = [](HPNS::ConnectDevice dv, HPNS::Internal::Base_NetworkObject* ns){
		std::cout << "\n client [" << ns->MSG_GetDeviceIP(dv) << "] Entry";
	};
	io.ClientLeaves = [](HPNS::ConnectDevice dv, HPNS::Internal::Base_NetworkObject* ns) {
		std::cout << "\n client [" << ns->MSG_GetDeviceIP(dv) << "] Leaves";
	};

	Server.push_command("resend", HPNS_COMMAND_HEAD{
		std::cout << "\n client call resend command";
		std::string message ="this is server your send mesage is :";
		message.append(data.get<std::string>());
		network_system->MSG_SendMessageToClient(device, "showmessage", message);
		});

	Server.Listen();

	while (true)
	{
		Server.Update();
	}
}


int main()
{
	std::cout << "Server Start";
	try
	{
		server_runing();
	}
	catch (const std::exception& err)
	{
		std::cout << "\n error message" << err.what();
	}

	std::cout << "\n Server Close";
}
```

## Client

```cpp
#include <HPNS.h>

HPNS_REG_COMMAND(server_message, [&](nlohmann::json& data, HPNS::Internal::Base_NetworkObject* network_system, HPNS::ConnectDevice device)
{
	std::cout << "\n Server Message " << data.get<std::string>();
	network_system->MSG_SendMessageToClient(device, "server_message", data);
})

void runing_network_system()
{
	HPNS::Context::CreateContext();
	HPNS::Client::TCP_IP4 client("114.34.88.237", 8080,0);//114.34.88.237

	auto& io = client.SetupCallback();
	io.ClientEntry = [](HPNS::ConnectDevice dv, HPNS::Internal::Base_NetworkObject* ns) {
		std::cout << "\n client [" << ns->MSG_GetDeviceIP(dv) << "] Entry";
		};
	io.ClientLeaves = [](HPNS::ConnectDevice dv, HPNS::Internal::Base_NetworkObject* ns) {
		std::cout << "\n client [" << ns->MSG_GetDeviceIP(dv) << "] Leaves";
		};


	client.push_command("showmessage", HPNS_COMMAND_HEAD{
		std::cout << "\n showmessage : " << data.get<std::string>();
	});

	client.Connect();
	

	while (true)
	{
		std::string cmd;
		std::string dt;

		std::cout << "\n command list : \n1.getip\n2.resend\n3.command_name\n\n";

		std::cout << "\n input command : ";
		std::cin >> cmd;
		std::cout << "\n input message : ";
		std::cin >> dt;

		client.Client_SendMessageToServer(cmd.c_str(), dt);
		client.Update();
	}
}


int main()
{
	std::cout << "\n Hello World This is Client";
	try
	{
		runing_network_system();
	}
	catch (const std::exception& err)
	{
		std::cout << "\n eror : " << err.what();
	}

	std::cout << "\n Client is close";
}
```
