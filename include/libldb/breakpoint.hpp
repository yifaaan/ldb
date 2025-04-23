#pragma once

#include <filesystem>

#include <libldb/types.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/stoppoint_collection.hpp>

namespace ldb
{
    class Target;
    class Breakpoint
    {
    public:  
        Breakpoint() = delete;
		Breakpoint(const Breakpoint&) = delete;
		Breakpoint& operator=(const Breakpoint&) = delete;
		Breakpoint(Breakpoint&&) = delete;
		Breakpoint& operator=(Breakpoint&&) = delete;
        virtual ~Breakpoint() = default;

        using IdType = std::int32_t;
        IdType Id() const { return id; }

        virtual void Enable() = 0;
        virtual void Disable() = 0;

        bool IsEnabled() const { return isEnabled; }
        bool IsHardware() const { return isHardware; }
        bool IsInternal() const { return isInternal; }

        virtual void Resolve() = 0;

        StoppointCollection<BreakpointSite, false>& BreakpointSites() { return breakpointSites; }
        const StoppointCollection<BreakpointSite, false>& BreakpointSites() const { return breakpointSites; }
        
        bool AtAddress(VirtAddr addr) const
        {
            return breakpointSites.ContainsAddress(addr);
        }

        bool InRange(VirtAddr low, VirtAddr high) const
        {
            return !breakpointSites.GetInRegion(low, high).empty();
        }
    protected:
        friend Target;
        Breakpoint(Target& _target, bool _isHardware = false, bool _isInternal = false);

        IdType id;
        Target* target;
        bool isEnabled = false;
        bool isHardware = false;
        bool isInternal = false;
        StoppointCollection<BreakpointSite, false> breakpointSites;
        BreakpointSite::IdType nextSiteId = 1;
    };

    class FunctionBreakpoint : public Breakpoint
    {
    public:
        void Resolve() override;
        std::string_view FunctionName() const { return functionName; }

    private:
        friend Target;
        FunctionBreakpoint(Target& _target, std::string _functionName, bool _isHardware = false, bool _isInternal = false)
            : Breakpoint(_target, _isHardware, _isInternal)
            , functionName(std::move(_functionName))
        {
            Resolve();
        }
        std::string functionName;
    };

    class LineBreakpoint : public Breakpoint
    {
    public:
        void Resolve() override;
        
        const std::filesystem::path& File() const { return file; }

        std::size_t Line() const { return line; }

    private:
        friend Target;
        LineBreakpoint(Target& _target, std::filesystem::path _file, std::size_t _line, bool _isHardware = false, bool _isInternal = false)
            : Breakpoint(_target, _isHardware, _isInternal)
            , file(std::move(_file))
            , line(_line)
        {
            Resolve();
        }
        
        std::filesystem::path file;
        std::size_t line;
    };

    class AddressBreakpoint : public Breakpoint
    {
    public:
        void Resolve() override;
        
        VirtAddr Address() const { return address; }

    private:
        friend Target;
        AddressBreakpoint(Target& _target, VirtAddr _address, bool _isHardware = false, bool _isInternal = false)
            : Breakpoint(_target, _isHardware, _isInternal)
            , address(_address)
        {
            Resolve();
        }
        
        VirtAddr address;
    };
}