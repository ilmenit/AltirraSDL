#pragma once

#include <memory>
#include <string>

class ATSimulator;
struct ATUIState;

// Main-thread rollback for media loads which need to change hardware/profile
// before the user can resolve a later validation prompt. Destruction only
// releases the backup; the caller explicitly commits or calls Restore().
class ATUIMediaSessionBackup {
public:
	ATUIMediaSessionBackup();
	~ATUIMediaSessionBackup();
	ATUIMediaSessionBackup(const ATUIMediaSessionBackup&) = delete;
	ATUIMediaSessionBackup& operator=(const ATUIMediaSessionBackup&) = delete;

	bool Capture(ATSimulator& sim, ATUIState& ui, std::string& error);
	bool Restore(std::string& error);
	void Commit(bool newGameProfile);
	void Discard();

private:
	struct Impl;
	std::unique_ptr<Impl> mpImpl;
};
