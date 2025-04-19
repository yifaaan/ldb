#pragma once

#include <vector>

#include <libldb/dwarf.hpp>

namespace ldb
{
    class Target;
    class Stack
    {
    public:
        explicit Stack(Target* _target)
            : target(_target)
        {}

        void ResetInlineHeight();

        std::vector<Die> InlineStackAtPc() const;

        std::uint32_t InlineHeight() const { return inlineHeight; }

        const Target& GetTarget() const { return *target; }

        void SimulateInlinedStepIn()
        {
            --inlineHeight;
        }

    private:
        Target* target{};
        std::uint32_t inlineHeight{};
    };
}