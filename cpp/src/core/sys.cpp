#include "sys.h"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

extern char **environ;

namespace qseq::sys {

bool readFile(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

bool writeFileAtomic(const std::string &path, const std::string &data, std::string &err)
{
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!(f << data)) {
            err = "não consegui gravar " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "rename " + path + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

bool mkdirs(const std::string &path, std::string &err)
{
    std::string cur;
    std::istringstream in(path);
    std::string part;
    if (!path.empty() && path[0] == '/')
        cur = "/";
    while (std::getline(in, part, '/')) {
        if (part.empty())
            continue;
        cur += part + "/";
        if (mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
            err = "mkdir " + cur + ": " + std::strerror(errno);
            return false;
        }
    }
    return true;
}

pid_t alivePid(const std::string &pidFile)
{
    std::string s;
    if (!readFile(pidFile, s))
        return 0;
    long pid = std::strtol(s.c_str(), nullptr, 10);
    if (pid <= 0 || pid == getpid())
        return 0;
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM)
        return 0;
    return static_cast<pid_t>(pid);
}

int run(const std::vector<std::string> &argv)
{
    std::vector<char *> args;
    for (const auto &a : argv)
        args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0)
        return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string selfPath()
{
    char buf[PATH_MAX];
#ifdef __FreeBSD__
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    size_t len = sizeof buf;
    if (sysctl(mib, 4, buf, &len, nullptr, 0) == 0)
        return buf;
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
#endif
    return "eq-tui";
}

} // namespace qseq::sys
