#include <libldb/stack.hpp>
#include <libldb/target.hpp>

namespace ldb
{
   std::vector<Die> Stack::InlineStackAtPc() const
   {
        auto pc = target->GetPcFileAddress();
        if (!pc.ElfFile()) return {};
        return pc.ElfFile()->GetDwarf().InlineStackAtAddress(pc);
   }

   void Stack::ResetInlineHeight()
   {
        auto stack = InlineStackAtPc();
        inlineHeight = 0;
        auto pc = target->GetPcFileAddress();
        for (auto it = stack.rbegin(); it != stack.rend() && it->LowPc() == pc; ++it)
        {
            ++inlineHeight;
        }
   }
}