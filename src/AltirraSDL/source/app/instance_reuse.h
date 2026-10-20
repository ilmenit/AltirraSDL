// Altirra - SDL desktop instance hand-off. GPL v2 or later.
#ifndef AT_INSTANCE_REUSE_H
#define AT_INSTANCE_REUSE_H

#include <string>
#include <vector>

struct ATInstanceReuseRequest {
	std::string mWorkingDirectory;
	std::vector<std::string> mArguments;
};

enum class ATInstanceReuseResult { Continue, Forwarded, Failed };

// Main thread only. Claims a per-user endpoint even when reuse is disabled,
// so enabling the persisted option also works for an already-running instance.
// Removes --singleinstance/--si/--nosingleinstance/--nosi from argv.
// Elevated Windows sessions always remain independent and publish no endpoint;
// the same user's lower-integrity processes must not control them via IPC.
ATInstanceReuseResult ATInstanceReuseInit(bool enabled, int& argc,
	char** argv, std::string& error);
bool ATInstanceReusePoll(ATInstanceReuseRequest& request);
void ATInstanceReuseShutdown();

// Uses the sender's working directory for path arguments without changing the
// receiver's process-wide working directory.
bool ATProcessForwardedCommandLineSDL3(int argc, char** argv,
	const char* workingDirectory);

#endif
