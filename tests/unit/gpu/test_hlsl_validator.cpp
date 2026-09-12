#include "core/gpu/shader/hlsl_validator.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu::core::gpu::shader;

#define HIV_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " msg " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Off-device HLSL validator]" << std::endl;

    // Valid VS-like source.
    auto ok = HlslValidator::Validate(
        "struct VSOutput { float4 out_pos : SV_Position; };\n"
        "VSOutput main(VSOutput input) { return input; }");
    HIV_ASSERT(ok.Passed(), "valid HLSL passes");
    HIV_ASSERT(ok.has_main, "has main");
    HIV_ASSERT(ok.balanced_braces, "braces balanced");
    HIV_ASSERT(ok.source_length > 16, "non-trivial source");
    std::cout << "  - valid source accepted: PASSED" << std::endl;

    // Missing entry point.
    auto no_main = HlslValidator::Validate("struct VSOutput { float4 out_pos : SV_Position; };");
    HIV_ASSERT(!no_main.Passed(), "missing main rejected");
    HIV_ASSERT(no_main.error.find("main") != std::string::npos, "reports missing main");
    std::cout << "  - missing main rejected: PASSED" << std::endl;

    // Unbalanced brace.
    auto unbal = HlslValidator::Validate("float4 main() { return float4(1,0,0,1);");
    HIV_ASSERT(!unbal.Passed(), "unbalanced rejected");
    HIV_ASSERT(!unbal.balanced_braces, "unbalanced braces flagged");
    std::cout << "  - unbalanced braces rejected: PASSED" << std::endl;

    // Empty source.
    auto empty = HlslValidator::Validate("");
    HIV_ASSERT(!empty.Passed(), "empty rejected");
    HIV_ASSERT(!empty.balanced_braces, "empty flagged");
    std::cout << "  - empty source rejected: PASSED" << std::endl;

    std::cout << "[Test: Off-device HLSL validator PASSED]" << std::endl;
    return 0;
}