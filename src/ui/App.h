#pragma once
#include "SFML/Graphics.hpp"
#include "../core/Emulator.h"
#include "../core/BatteryStore.h"
#include "../frontend/InputMapper.h"
#include "../frontend/RunState.h"
#include "../frontend/FramePacer.h"
#include "../frontend/SaveStateSlots.h"
#include "AudioOutput.h"
#include <csignal>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The emulator's window: runs the game at the Game Boy's speed, shows its screen and plays its sound, and
// handles the controls (see Controls.cpp)
class App
{
public:
	struct Options
	{
		std::string RomPath;

		// The window's size as a multiple of the Game Boy's screen
		int Scale;

		// If not empty, every frame is appended to this file as raw 160x144 RGBA
		std::string RecordPath;

		// The Game Boy to run the game on, or none for the one its header asks for (see ModelForRom)
		std::optional<GameBoyModel> Model;
	};

	App(const Options& options, std::shared_ptr<Cartridge> cartridge, std::string gameTitle, BatteryStore& saves);

	// Runs until the window is closed or quitRequested is set
	void Run(const volatile std::sig_atomic_t& quitRequested);

	// Shows a save's error or warning, if it has one, in the terminal
	static void ReportSaveResult(const BatteryStore::Result& result);

private:
	Options _options;
	std::shared_ptr<Cartridge> _cartridge;
	std::string _gameTitle;
	BatteryStore& _saves;
	SaveStateSlots _stateSlots;

	std::unique_ptr<Emulator> _emulator;
	AudioOutput _audio;
	std::vector<short> _samples;
	std::ofstream _record;

	InputMapper _input;
	RunState _runState;
	FramePacer _pacer;
	sf::Clock _clock;
	int _framesUntilSave;

	sf::RenderWindow _window;
	std::string _windowTitle;
	bool _fullscreen{ false };

	// The emulator's latest frame, and the screen it builds up in (see RunFrame)
	sf::Texture _frameTexture;
	sf::Sprite _frameSprite;
	sf::RenderTexture _screen;
	sf::Sprite _screenSprite;

	int64_t Now() const { return _clock.getElapsedTime().asMicroseconds(); }

	void OpenWindow();
	void FitScreenToWindow();
	void UpdateTitle();

	void HandleEvents();
	void DoCommand(Command command);

	void RunFrame(int speed);
	void ShowFrame(const int* frame);
	void Present();

	// Shows a message in the window title, and in the terminal if log is set
	void ShowMessage(const std::string& message, bool log = true);

	// A newly powered-on Game Boy of the chosen model
	std::unique_ptr<Emulator> PowerOn() const;

	void Reset();
	void SaveToSlot();
	void LoadFromSlot();
	void ShowSlot();
	void SaveScreenshot();
	void ToggleFullscreen();
};
