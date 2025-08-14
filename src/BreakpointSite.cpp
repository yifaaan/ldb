#include <libldb/BreakpointSite.h>

namespace
{
    auto GetNextId()
    {
        static ldb::BreakpointSite::IdType id = 0;
        return ++id;
    }
} // namespace

namespace ldb
{
    BreakpointSite::BreakpointSite(Process& proc, VirtAddr addr)
        : process(&proc), address(addr), isEnabled(false), savedData{}
    {
        id = GetNextId();
    }
} // namespace ldb