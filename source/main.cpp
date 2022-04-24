#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <eiface.h>
#include <dbg.h>
#include <filewatch.hpp>
#include <queue>
#include <mutex>
#include <cstring>

typedef std::pair<std::string, filewatch::Event> FileChange;

filewatch::FileWatch* watcher = nullptr;
std::mutex changes_mtx;
std::queue<FileChange> file_changes{};

std::string get_game_path(GarrysMod::Lua::ILuaBase* LUA) 
{
	SourceSDK::FactoryLoader engine_loader("engine");
	IVEngineServer* engine_server = engine_loader.GetInterface<IVEngineServer>(INTERFACEVERSION_VENGINESERVER);
	if (engine_server == nullptr)
		LUA->ThrowError("Failed to load required IVEngineServer interface");

	std::string game_dir;
	game_dir.resize(1024);
	engine_server->GetGameDir(&game_dir[0], static_cast<int32_t>(game_dir.size()));
	game_dir.resize(std::strlen(game_dir.c_str()));

	return game_dir;
}

void hook_run(lua_State* state, const char* path, const char* event_type)
{
	if (path == nullptr || event_type == nullptr) return;

	GarrysMod::Lua::ILuaBase* LUA = state->luabase;
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Run");
				LUA->PushString("FileChanged");
				LUA->PushString(path);
				LUA->PushString(event_type);
			LUA->PCall(3, 0, 0);
	LUA->Pop(2);
}

int spew_file_events(lua_State* state)
{
	changes_mtx.lock();
	while (!file_changes.empty())
	{
		FileChange change = file_changes.front();
		file_changes.pop();

		const char* event_type;
		switch (change.second)
		{
			case filewatch::Event::CREATED:
				event_type = "CREATED";
				break;
			case filewatch::Event::DELETED:
				event_type = "DELETED";
				break;
			case filewatch::Event::CHANGED:
				event_type = "CHANGED";
				break;
			case filewatch::Event::RENAMED_NEW:
				event_type = "RENAMED_NEW";
				break;
			case filewatch::Event::RENAMED_OLD:
				event_type = "RENAMED_OLD";
				break;
			default:
				event_type = "UNKNOWN";
				break;
		}

		hook_run(state, change.first.c_str(), event_type);
	}
	changes_mtx.unlock();

	return 0;
}

void create_dispatcher(GarrysMod::Lua::ILuaBase* LUA)
{
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "timer");
			LUA->GetField(-1, "Create");
				LUA->PushString("IOSpewFileEvents");
				LUA->PushNumber(0.250);
				LUA->PushNumber(0);
				LUA->PushCFunction(spew_file_events);
				LUA->PCall(4, 0, 0);
	LUA->Pop(2);
}

void destroy_dispatcher(GarrysMod::Lua::ILuaBase* LUA)
{
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "timer");
			LUA->GetField(-1, "Destroy");
				LUA->PushString("IOSpewFileEvents");
			LUA->PCall(1, 0, 0);
	LUA->Pop(2);
}

GMOD_MODULE_OPEN()
{
	watcher = new filewatch::FileWatch(get_game_path(LUA), [](std::string path, const filewatch::Event event_type) {
		std::replace(path.begin(), path.end(), '\\', '/');

		changes_mtx.lock();
		file_changes.push(FileChange(path, event_type));
		changes_mtx.unlock();
	});

	create_dispatcher(LUA);

	return 0;
}

GMOD_MODULE_CLOSE()
{
	destroy_dispatcher(LUA);

	if (watcher != nullptr) 
		watcher->~FileWatch();

	file_changes.~queue();
	changes_mtx.~mutex();

	return 0;
}

// test github-actions compile
