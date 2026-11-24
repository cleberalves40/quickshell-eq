// Arquivos, pids e processos (sem shell).
#ifndef QSEQ_SYS_H
#define QSEQ_SYS_H

#include <string>
#include <sys/types.h>
#include <vector>

namespace qseq::sys {

bool readFile(const std::string &path, std::string &out);
bool writeFileAtomic(const std::string &path, const std::string &data, std::string &err);
bool mkdirs(const std::string &path, std::string &err);

/** PID lido do arquivo, se o processo existir; senão 0. */
pid_t alivePid(const std::string &pidFile);

/** Executa argv (posix_spawnp, sem shell) e espera; devolve o código de saída ou -1. */
int run(const std::vector<std::string> &argv);

/** Caminho absoluto deste executável. */
std::string selfPath();

} // namespace qseq::sys

#endif
