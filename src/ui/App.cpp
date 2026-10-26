#include "stdafx.h"
#include "App.h"
#include "Controls.h"
#include "../frontend/Letterbox.h"
#include "../frontend/ScreenshotNamer.h"
#include <iostream>

namespace
{
	// The Game Boy's frame rate (4194304 / 70224 = 59.73 Hz)
	const int64_t FrameTime = 1000000LL * Graphics::FrameClocks / Graphics::SystemClockHz;

	// Changes are saved within a few seconds, so a crash loses little, without writing the file on every frame of a
	// game's multi-frame save routine
	const int FramesBetweenSaves = 3 * Graphics::SystemClockHz / Graphics::FrameClocks;
}

void App::ReportSaveResult(const BatteryStore::Result& result)
{
	if (!result.Message.empty()) std::cerr << result.Message << std::endl;
}

App::App(const Options& options, std::shared_ptr<Cartridge> cartridge, std::string gameTitle, BatteryStore& saves) :
	_options(options), _cartridge{ std::move(cartridge) }, _gameTitle{ std::move(gameTitle) }, _saves(saves),
	_emulator{ std::make_unique<Emulator>(_cartridge) }, _audio{ _emulator->GetAudioSampleRate() },
	_input{ Controls::CreateInputMapper() }, _pacer{ FrameTime }, _framesUntilSave{ FramesBetweenSaves }
{
	if (!_options.RecordPath.empty()) _record.open(_options.RecordPath, std::ios::binary);

	_frameTexture.create(Graphics::HozPixels, Graphics::VertPixels);
	_frameSprite.setTexture(_frameTexture);

	// Frames are drawn at 60% opacity over the previous ones to emulate the slow LCD. They build up in a texture
	// the app owns, as a window's back buffer isn't guaranteed to keep its contents between frames
	_screen.create(Graphics::HozPixels, Graphics::VertPixels);
	_screen.setSmooth(true);
	_screen.clear();
	_screenSprite.setTexture(_screen.getTexture());

	OpenWindow();
}

void App::OpenWindow()
{
	_windowTitle = _runState.WindowTitle(_gameTitle);

	if (_fullscreen)
	{
		_window.create(sf::VideoMode::getDesktopMode(), _windowTitle, sf::Style::Fullscreen);
	}
	else
	{
		_window.create(sf::VideoMode(Graphics::HozPixels * _options.Scale, Graphics::VertPixels * _options.Scale),
			_windowTitle);
	}

	// Commands act once per press rather than repeating while their key is held
	_window.setKeyRepeatEnabled(false);
	_window.setMouseCursorVisible(!_fullscreen);
	FitScreenToWindow();

	// Keys held while the window was replaced won't send their release events to the new one
	_input.ReleaseKeys();
}

void App::FitScreenToWindow()
{
	auto size = _window.getSize();
	auto box = Letterbox::Fit(size.x, size.y, Graphics::HozPixels, Graphics::VertPixels);

	sf::View view{ sf::FloatRect(0.f, 0.f, static_cast<float>(Graphics::HozPixels), static_cast<float>(Graphics::VertPixels)) };
	view.setViewport({ box.Left, box.Top, box.Width, box.Height });
	_window.setView(view);
}

void App::UpdateTitle()
{
	auto title = _runState.WindowTitle(_gameTitle);
	if (title == _windowTitle) return;

	_windowTitle = title;
	_window.setTitle(title);
}

void App::Run(const volatile std::sig_atomic_t& quitRequested)
{
	while (_window.isOpen())
	{
		if (quitRequested)
		{
			_window.close();
			break;
		}

		HandleEvents();
		if (!_window.isOpen()) break;

		_runState.SetFastForward(_input.IsHeld(Command::FastForward));
		UpdateTitle();

		auto speed = _runState.GetSpeed();
		if (_runState.IsPaused())
		{
			// Still drawn, so the window repaints after being resized or switched to fullscreen
			Present();
		}
		else
		{
			RunFrame(speed);
		}

		auto wait = _pacer.FrameDone(Now(), speed);
		if (wait > 0) sf::sleep(sf::microseconds(wait));
	}
}

void App::HandleEvents()
{
	sf::Event event;
	while (_window.pollEvent(event))
	{
		switch (event.type)
		{
		case sf::Event::Closed:
			_window.close();
			return;

		case sf::Event::Resized:
			FitScreenToWindow();
			break;

		case sf::Event::LostFocus:
			_input.ReleaseKeys();
			break;

		default:
			DoCommand(Controls::HandleEvent(event, _input));
			break;
		}
	}
}

void App::DoCommand(Command command)
{
	switch (command)
	{
	case Command::TogglePause:
		_runState.TogglePause();
		if (_runState.IsPaused()) _audio.Clear();
		break;

	case Command::Reset: Reset(); break;
	case Command::Screenshot: SaveScreenshot(); break;
	case Command::ToggleFullscreen: ToggleFullscreen(); break;
	case Command::LeaveFullscreen: if (_fullscreen) ToggleFullscreen(); break;

	// Fast-forward lasts while its key is held, so is read in Run
	case Command::FastForward:
	case Command::None:
		break;
	}
}

void App::RunFrame(int speed)
{
	// Joypad state comes from key events rather than sf::Keyboard::isKeyPressed, which on macOS needs the Input
	// Monitoring permission
	_emulator->GetJoypad().SetKeysDown(_window.hasFocus() ? _input.GetKeysDown() : JoypadKey::NoKey);

	auto frame = _emulator->GetFrame();
	_frameTexture.update(reinterpret_cast<sf::Uint8*>(frame));
	_screen.draw(_frameSprite);
	_screen.display();
	if (_record) _record.write(reinterpret_cast<char*>(frame), Graphics::HozPixels * Graphics::VertPixels * sizeof(*frame));

	// Fast-forward is silent: splicing together one frame's sound in every few buzzes at the frame rate, and playing
	// all of it would build up a backlog. The samples are still taken so they don't pile up in the APU
	_emulator->TakeAudioSamples(_samples);
	if (speed == 1) _audio.Push(_samples);
	else _audio.Clear();
	if (_audio.getStatus() != sf::SoundStream::Playing) _audio.play();

	// Fast-forward only shows frames at the normal frame rate, as showing more would only cost time
	if (_pacer.ShouldPresent(Now(), speed)) Present();

	if (--_framesUntilSave == 0)
	{
		ReportSaveResult(_saves.SaveIfChanged());
		_framesUntilSave = FramesBetweenSaves;
	}
}

void App::Present()
{
	_window.clear(sf::Color::Black);
	_window.draw(_screenSprite);
	_window.display();
}

void App::Reset()
{
	// Like switching the Game Boy off and on: the game starts again, keeping what the battery keeps
	ReportSaveResult(_saves.SaveIfChanged());
	_audio.Clear();
	_cartridge->Reset();
	_emulator = std::make_unique<Emulator>(_cartridge);

	// The old game's last frames shouldn't fade out over the new one's first
	_screen.clear();
	_screen.display();
}

void App::SaveScreenshot()
{
	// The emulator's frames are 60% opaque for the slow-LCD effect; screenshots shouldn't be
	auto image = _frameTexture.copyToImage();
	auto size = image.getSize();
	for (unsigned y = 0; y < size.y; y++)
	{
		for (unsigned x = 0; x < size.x; x++)
		{
			auto pixel = image.getPixel(x, y);
			pixel.a = 255;
			image.setPixel(x, y, pixel);
		}
	}

	// Next to the ROM, or in the working directory if that fails (e.g. a read-only ROM folder)
	for (auto& base : { ScreenshotNamer::BaseForRom(_options.RomPath), ScreenshotNamer::NameForRom(_options.RomPath) })
	{
		auto path = ScreenshotNamer::NextPath(base);
		if (!path.empty() && image.saveToFile(path))
		{
			std::cerr << "Saved screenshot " << path << std::endl;
			return;
		}
	}

	std::cerr << "Couldn't save a screenshot" << std::endl;
}

void App::ToggleFullscreen()
{
	_fullscreen = !_fullscreen;
	OpenWindow();
}
