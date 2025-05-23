#include <libldb/breakpoint.hpp>

namespace
{
    auto get_next_id()
    {
        static ldb::breakpoint_site::id_type id = 0;
        return ++id;
    }
} // namespace

ldb::breakpoint::breakpoint(target& tgt, bool is_hardware, bool is_internal)
    : target_{&tgt}
    , is_hardware_{is_hardware}
    , is_internal_{is_internal}
{
    id_ = is_internal ? -1 : get_next_id();
}

void ldb::breakpoint::enable()
{
    is_enabled_ = true;
    breakpoint_sites_.for_each(
    [](auto& site)
    {
        site.enable();
    });
}

void ldb::breakpoint::disable()
{
    is_enabled_ = false;
    breakpoint_sites_.for_each(
    [](auto& site)
    {
        site.disable();
    });
}
