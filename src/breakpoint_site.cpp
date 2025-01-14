#include <libldb/breakpoint_site.hpp>

namespace
{
    auto GetNextId()
    {
        static ldb::BreakpointSite::IdType id = 0;
        return ++id;
    }
}

ldb::BreakpointSite::BreakpointSite(Process& proc, VirtAddr _address)
        :process(&proc)
        ,address(_address)
        ,isEnable(false)
        ,savedData{}
{
    id = GetNextId();
}