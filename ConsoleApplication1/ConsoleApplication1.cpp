// ReSharper disable once CppInconsistentNaming
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <Windows.h>
#include <string>
#include <ctime>
#include "discord/discord.h"
#include <iomanip>


namespace StudioOneRichPresence {
using namespace discord;
  Core* core {};
  HWINEVENTHOOK windowChangeListener;
  HWINEVENTHOOK destroyListener;
  
  inline const char* ToLevelString(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "[Debug] ";
    case LogLevel::Warn: return "[Warn] ";
    case LogLevel::Error: return "[Error] ";
    default: return "[Info] ";
    }
  }
  
  void Log(const char* message, LogLevel level = LogLevel::Info) {
    time_t rawTime;
    time ( &rawTime );
    const std::tm* timeInfo = localtime(&rawTime);
    std::cout << std::put_time(timeInfo, "[%H:%M:%S] ") << ToLevelString(level) << message << "\n";
  }

  void Log(LogLevel level, std::initializer_list<const char*> message) {
    auto string = std::string();
    for (const char* element : message) {
      string.append(element);
    }
    Log(string.c_str(), level);
  }

  void SetupDiscord() {
    Log("Starting up Discord");
    auto result = Core::Create(468793812500611073, DiscordCreateFlags_Default, &core);
    if (!core || result != Result::Ok) {
      Log(LogLevel::Error, {"Failed to instantiate discord core! (err ", std::to_string(static_cast<int>(result)).c_str(), ")"});
      exit(1);
    }
    core->SetLogHook(LogLevel::Debug, [](LogLevel level, const char* message) {
        Log(message, level);
    });
    boolean lock = false;
    core->UserManager().OnCurrentUserUpdate.Connect([&lock]() {
      User current {};
      auto result2 = core->UserManager().GetCurrentUser(&current);
      if(result2 != Result::Ok) {
        Log(LogLevel::Error, {"Could not get User! (", std::to_string(static_cast<int>(result2)).c_str(), ")"});
        exit(2);
      }
      Log(LogLevel::Info, {"Successfully set up Discord! (logged in as ", current.GetUsername(), ")"});
      lock = true;
    });
    while(!lock) core->RunCallbacks();
  }

  void UpdatePresence(bool isEditing, const char* songName) {
      Activity activity {};
      activity.SetDetails(isEditing ? "Producing / Composing" : "Idle");
      if(isEditing)activity.SetState(std::string("Song name: ").append(songName).c_str());
      activity.SetType(ActivityType::Playing);
      activity.GetAssets().SetLargeImage("large");
      activity.GetAssets().SetLargeText("Studio One");
      if(isEditing) activity.GetTimestamps().SetStart(std::time(nullptr));
      boolean lock = true;
      core->ActivityManager().UpdateActivity(activity, [&lock](Result result) {
        if (result != Result::Ok) {
          std::cout << static_cast<int>(result) << "\n";
        }
        lock = false;
      });
      while (lock) {
        core->RunCallbacks();
      }
  }

  void CALLBACK WindowCallback(HWINEVENTHOOK, DWORD event, const HWND hwnd, long, long, DWORD, DWORD) {
    const auto len = GetWindowTextLengthA(hwnd) + 1;
    const auto buffer = new char[len];
    GetWindowTextA(hwnd, buffer, len);
    const auto s = std::string(buffer);
    if (s.rfind("Studio One", 0) != 0) return;
    if(event == EVENT_OBJECT_DESTROY) {
      UnhookWinEvent(windowChangeListener);
      UnhookWinEvent(destroyListener);
      Log("Shutting down Application as Studio One was closed.");
      exit(0);
    }
    const auto isEditing = s.length() > 10;
    UpdatePresence(isEditing, isEditing ? s.substr(13).c_str() : "");
  }

  BOOL CALLBACK SaveWindow(const HWND hwnd, LPARAM) {
    const auto len = GetWindowTextLengthA(hwnd) + 1;
    const auto buffer = new char[len];
    GetWindowTextA(hwnd, buffer, len);
    const auto s = std::string(buffer);
    if (s.rfind("Studio One", 0) != 0) return 1;
    Log("Found Studio One!");
    const auto isEditing = s.length() > 10;
    UpdatePresence(isEditing, isEditing ? s.substr(13).c_str() : "");
    windowChangeListener = SetWinEventHook(EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE, nullptr, WindowCallback, GetProcessId(hwnd), GetThreadId(hwnd), WINEVENT_OUTOFCONTEXT);
    destroyListener = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, WindowCallback, GetProcessId(hwnd), GetThreadId(hwnd), WINEVENT_OUTOFCONTEXT);
    return 0;
  }

  void PollEvents() {
    while (true) {
      core->RunCallbacks();
      MSG msg;
      while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
  }

}

int main() {
  try {
  StudioOneRichPresence::SetupDiscord();
  EnumWindows(StudioOneRichPresence::SaveWindow, reinterpret_cast<LPARAM>(nullptr));
  if(!StudioOneRichPresence::destroyListener) {
    StudioOneRichPresence::Log("Could not find Studio One, shutting down application.", discord::LogLevel::Warn);
    exit(0);
  }
  StudioOneRichPresence::PollEvents();
  } catch (...) {
    return -1;
  }
  return 0;
}
