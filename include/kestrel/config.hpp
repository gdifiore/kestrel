#pragma once

#include <string>
#include <filesystem>

namespace kestrel
{

	struct UiInputs;

	// Path to persisted config file. Honors $XDG_CONFIG_HOME, falls back to
	// $HOME/.config/kestrel/config.ini. Does not create parent dirs.
	std::filesystem::path config_path();

	void load_config(UiInputs &in);

	bool save_config(const UiInputs &in);

} // namespace kestrel
