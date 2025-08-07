#include <libldb/stack.hpp>
#include <libldb/target.hpp>

namespace ldb {
std::vector<Die> Stack::InlineStackAtPc() const {
  auto pc = target->GetPcFileAddress();
  if (!pc.ElfFile()) return {};
  return pc.ElfFile()->GetDwarf().InlineStackAtAddress(pc);
}

void Stack::ResetInlineHeight() {
  auto stack = InlineStackAtPc();
  inlineHeight = stack.size() > 0 ? stack.size() - 1 : 0;
}
}  // namespace ldb