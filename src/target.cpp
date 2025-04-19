#include <signal.h>
#include <format>

#include <libldb/target.hpp>

namespace
{
    std::unique_ptr<ldb::Elf> CreateLoadedElf(const ldb::Process& process, const std::filesystem::path& path)
    {
        auto auxv = process.GetAuxv();
        auto obj = std::make_unique<ldb::Elf>(path);
        // set load bias
        obj->NotifyLoaded(ldb::VirtAddr{auxv[AT_ENTRY] - obj->GetHeader().e_entry});
        return obj;
    }
}

namespace ldb
{
    std::unique_ptr<Target> Target::Launch(std::filesystem::path path, std::optional<int> stdoutReplacement)
    {
        auto process = Process::Launch(path, true, stdoutReplacement);
        auto obj = CreateLoadedElf(*process, path);
        auto target = std::unique_ptr<Target>(new Target{std::move(process), std::move(obj)});
        target->GetProcess().SetTarget(target.get());
        return target;
    }

    std::unique_ptr<Target> Target::Attach(pid_t pid)
    {
        auto path = std::format("/proc/{}/exe", pid);
        auto process = Process::Attach(pid);
        auto obj = CreateLoadedElf(*process, path);
        auto target = std::unique_ptr<Target>(new Target{std::move(process), std::move(obj)});
        target->GetProcess().SetTarget(target.get());
        return target;
    }

    void Target::NotifyStop(const ldb::StopReason& reason)
    {
        stack.ResetInlineHeight();
    }

    FileAddr Target::GetPcFileAddress() const
    {
        return process->GetPc().ToFileAddr(*elf);
    }

    LineTable::iterator Target::LineEntryAtPc() const
    {
        auto pc = GetPcFileAddress();
        if (!pc.ElfFile()) return {};
        auto cu = pc.ElfFile()->GetDwarf().CompileUnitContainingAddress(pc);
        if (!cu) return {};
        return cu->Lines().GetEntryByAddress(pc);
    }

    StopReason Target::RunUntilAddress(VirtAddr address)
    {
        BreakpointSite* breakpointToRemove = nullptr;
        if (!process->BreakpointSites().ContainsAddress(address))
        {
            breakpointToRemove = &process->CreateBreakpointSite(address, false, true);
            breakpointToRemove->Enable();
        }
        process->Resume();
        auto reason = process->WaitOnSignal();
        if (reason.IsBreakpoint() && process->GetPc() == address)
        {
            reason.trapReason = TrapType::singleStep;
        }
        if (breakpointToRemove)
        {
            process->BreakpointSites().RemoveByAddress(address);
        }
        return reason;
    }

    StopReason Target::StepIn()
    {
        auto& stack = GetStack();
        if (stack.InlineHeight() > 0)
        {
            stack.SimulateInlinedStepIn();
            return StopReason{ProcessState::stopped, SIGTRAP, TrapType::singleStep};
        }
        auto origLine = LineEntryAtPc();
        do
        {
            auto reason = process->StepInstruction();
            if (!reason.IsStep())
            {
                return reason;
            }
        } while ((LineEntryAtPc() == origLine || LineEntryAtPc()->endSequence) && LineEntryAtPc() != LineTable::iterator{});
        auto pc = GetPcFileAddress();
        if (pc.ElfFile())
        {
            auto& dwarf = pc.ElfFile()->GetDwarf();
            auto func = dwarf.FunctionContainingAddress(pc);
            if (func && func->LowPc() == pc)
            {
                auto line = LineEntryAtPc();
                if (line != LineTable::iterator{})
                {
                    ++line;
                    return RunUntilAddress(line->address.ToVirtAddr());
                }
            }   
        }
        return StopReason{ProcessState::stopped, SIGTRAP, TrapType::singleStep};
    }

    StopReason Target::StepOut()
    {

    }
    
    StopReason Target::StepOver()
    {

    }
}