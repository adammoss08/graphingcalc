#include "Test.hpp"

#include "math/Analyzer.hpp"
#include "math/MathTree.hpp"

#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include <cmath>
#include <string>

using namespace calc;

namespace {

// Runs `p` on the GPU VM (shaders/vm.h) at each point.
std::vector<float> gpuEval(const Program& p, const std::vector<simd_float2>& pts) {
    static MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    static MTL::Library* lib = [] {
        NS::Error* err = nullptr;
        return dev->newLibrary(NS::String::string(METALLIB_PATH, NS::UTF8StringEncoding), &err);
    }();
    static MTL::ComputePipelineState* pipe = [] {
        NS::Error* err = nullptr;
        auto* fn = lib->newFunction(NS::String::string("vmEvalPoints", NS::UTF8StringEncoding));
        return dev->newComputePipelineState(fn, &err);
    }();
    static MTL::CommandQueue* q = dev->newCommandQueue();
    std::vector<float> consts;
    for (double c : p.consts) consts.push_back(float(c));
    if (consts.empty()) consts.push_back(0);
    uint32_t len = uint32_t(p.code.size()), count = uint32_t(pts.size());
    auto* codeB = dev->newBuffer(p.code.data(), std::max<size_t>(4, p.code.size() * 4), MTL::ResourceStorageModeShared);
    auto* constB = dev->newBuffer(consts.data(), consts.size() * 4, MTL::ResourceStorageModeShared);
    auto* ptB = dev->newBuffer(pts.data(), pts.size() * sizeof(simd_float2), MTL::ResourceStorageModeShared);
    auto* outB = dev->newBuffer(pts.size() * 4, MTL::ResourceStorageModeShared);
    auto* cb = q->commandBuffer();
    auto* ce = cb->computeCommandEncoder();
    ce->setComputePipelineState(pipe);
    ce->setBuffer(codeB, 0, 0);
    ce->setBuffer(constB, 0, 1);
    ce->setBytes(&len, 4, 2);
    ce->setBuffer(ptB, 0, 3);
    ce->setBuffer(outB, 0, 4);
    ce->setBytes(&count, 4, 5);
    ce->dispatchThreads(MTL::Size(count, 1, 1), MTL::Size(64, 1, 1));
    ce->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();
    std::vector<float> out(static_cast<float*>(outB->contents()), static_cast<float*>(outB->contents()) + count);
    codeB->release(); constB->release(); ptB->release(); outB->release();
    return out;
}

Program implicitProgram(const char* latex) {
    MathTree t;
    t.insertLatex(latex);
    std::vector<RowInput> in{{&t}};
    auto r = analyzeGraph(in, {});
    return r[0].pieces.empty() ? Program{} : r[0].pieces[0].f;
}

} // namespace

// Every opcode family evaluated by the GPU VM must agree with the CPU VM (float tolerance).
// Not covered by design: variable exponents of negative bases, e.g. (-8)^(x/3). The CPU applies
// Desmos's runtime rational check in double; float can't tell such exponents from fractions.
TEST(gpu_vm_matches_cpu_vm) {
    const char* cases[] = {
        "x+y=0", "x-y\\cdot 2=0", "\\frac{x}{y}=1", "x^{3}=y", "x^{\\frac{1}{3}}=y", "\\sqrt{x}=y",
        "\\sqrt[3]{x}=y", "\\left|x\\right|=y", "\\operatorname{sign}\\left(x\\right)=y",
        "\\operatorname{floor}\\left(x\\right)=y", "\\operatorname{ceil}\\left(x\\right)=y",
        "\\operatorname{round}\\left(x\\right)=y", "\\sin x=y", "\\cos x=y", "\\tan x=y", "\\sec x=y",
        "\\csc x=y", "\\cot x=y", "\\arcsin\\left(\\frac{x}{10}\\right)=y", "\\arccos\\left(\\frac{x}{10}\\right)=y",
        "\\arctan x=y", "\\sinh\\left(\\frac{x}{3}\\right)=y", "\\cosh\\left(\\frac{x}{3}\\right)=y", "\\tanh x=y",
        "e^{\\frac{x}{3}}=y", "\\ln x=y", "\\log x=y", "\\log_{2}x=y", "\\min\\left(x,y\\right)=1",
        "\\max\\left(x,y\\right)=1", "\\operatorname{mod}\\left(x,3\\right)=y", "x^{2}+y^{2}=r", "\\theta=1",
        "\\left\\{x>0:x,-x\\right\\}=y", "\\left\\{0<x<2\\right\\}=y", "x^{\\frac{2}{3}}+y^{\\frac{2}{3}}=1",
        "x^{\\frac{1}{3}}=y", "x^{-\\frac{2}{5}}=y",
        "\\left(\\frac{x}{2}\\right)!=y", "\\sum_{n=1}^{4}\\frac{\\sin\\left(nx\\right)}{n}=y",
        "\\prod_{k=1}^{3}\\left(x-k\\right)=y", "\\int_{0}^{x}\\cos\\left(ty\\right)dt=1",
        "\\sum_{n=1}^{y}n=x", "\\operatorname{nCr}\\left(10,3\\right)=y+x",
    };
    std::vector<simd_float2> pts;
    for (int i = 0; i < 64; ++i) pts.push_back(simd_make_float2(-7.3f + i * 0.23f, -3.1f + (i % 13) * 0.57f));
    for (const char* c : cases) {
        Program p = implicitProgram(c);
        if (p.empty()) { test::fail(__FILE__, __LINE__, std::string("no program for ") + c); continue; }
        std::vector<float> g = gpuEval(p, pts);
        for (size_t i = 0; i < pts.size(); ++i) {
            double cpu = evaluate(p, pts[i].x, pts[i].y);
            double gpu = g[i];
            bool ok = (std::isnan(cpu) && std::isnan(gpu)) ||
                      (std::isinf(cpu) && std::isinf(gpu) && (cpu > 0) == (gpu > 0)) ||
                      std::fabs(cpu - gpu) <= 2e-4 * (1 + std::fabs(cpu)) ||
                      std::fabs(cpu) > 1e5;  // near poles float vs double legitimately differ
            if (!ok) {
                char buf[200];
                std::snprintf(buf, sizeof buf, "%s at (%g,%g): cpu %.9g gpu %.9g", c, pts[i].x, pts[i].y, cpu, gpu);
                test::fail(__FILE__, __LINE__, buf);
                break;
            }
        }
    }
}
