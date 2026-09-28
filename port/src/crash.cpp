// Crash reporting: prints the faulting address and a backtrace for fatal
// signals. Faults below 4GB are almost always an untranslated Wii address
// (a u32 used as a pointer without U32_TO_PTR).

#include "port/memory.hpp"

#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {

void writeStr(const char* s) { (void)!write(STDERR_FILENO, s, strlen(s)); }

void onFatalSignal(int sig, siginfo_t* info, void* uctx) {
    char buf[256];
    const uintptr_t addr = reinterpret_cast<uintptr_t>(info->si_addr);
    snprintf(buf, sizeof(buf), "\n*** fatal signal %d (%s) at address %#lx\n", sig, strsignal(sig),
             static_cast<unsigned long>(addr));
    writeStr(buf);
    if (addr >= 0x80000000ull && addr < 0x100000000ull) {
        snprintf(buf, sizeof(buf),
                 "*** looks like an untranslated Wii address (host would be %#lx); a u32 was used as a pointer\n",
                 static_cast<unsigned long>(gPortWiiBase + (addr - 0x80000000ull)));
        writeStr(buf);
    }
    // Walk the frame-pointer chain from the faulting context. backtrace()
    // stops at the end of the thread's registered stack, and game threads run
    // on stacks inside the Wii address window.
    auto* uc = static_cast<ucontext_t*>(uctx);
    uintptr_t pc = uc->uc_mcontext->__ss.__pc;
    uintptr_t lr = uc->uc_mcontext->__ss.__lr;
    uintptr_t fp = uc->uc_mcontext->__ss.__fp;
    auto printFrame = [&](int i, uintptr_t addr) {
        Dl_info di;
        if (dladdr(reinterpret_cast<void*>(addr), &di) && di.dli_sname != nullptr) {
            snprintf(buf, sizeof(buf), "  #%-2d %#lx %s + %lu\n", i, static_cast<unsigned long>(addr), di.dli_sname,
                     static_cast<unsigned long>(addr - reinterpret_cast<uintptr_t>(di.dli_saddr)));
        } else {
            snprintf(buf, sizeof(buf), "  #%-2d %#lx\n", i, static_cast<unsigned long>(addr));
        }
        writeStr(buf);
    };
    printFrame(0, pc);
    printFrame(1, lr);
    for (int i = 2; i < 48 && fp != 0 && (fp & 0xF) == 0; i++) {
        const uintptr_t* rec = reinterpret_cast<const uintptr_t*>(fp);
        const uintptr_t nextFp = rec[0];
        const uintptr_t ret = rec[1];
        if (ret == 0) {
            break;
        }
        printFrame(i, ret);
        if (nextFp <= fp) {
            break;
        }
        fp = nextFp;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

struct Installer {
    Installer() {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = onFatalSignal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        static char altStack[64 * 1024];
        stack_t ss{};
        ss.ss_sp = altStack;
        ss.ss_size = sizeof(altStack);
        sigaltstack(&ss, nullptr);
        for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT}) {
            sigaction(sig, &sa, nullptr);
        }
    }
} sInstaller;

}  // namespace
