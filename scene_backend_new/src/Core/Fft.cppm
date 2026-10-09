export module owe.fft;

import rstd;

export namespace owe::fft
{

using namespace rstd::prelude;

struct Complex32 {
    float real = 0.0f;
    float imag = 0.0f;
};

class DftPlan32;

class DftWorkspace32 {
    friend class DftPlan32;

    Vec<Complex32> first_;
    Vec<Complex32> second_;
};

class DftPlan32 {
public:
    explicit DftPlan32(rstd::size_t size);

    rstd::size_t size() const noexcept { return size_; }
    rstd::size_t convolution_size() const noexcept { return convolution_size_; }

    void forward(rstd::slice<Complex32> input, rstd::mut_ref<Complex32[]> output,
                 DftWorkspace32& workspace) const;

private:
    void power_forward(rstd::slice<Complex32> input, rstd::mut_ref<Complex32[]> output) const;

    rstd::size_t     size_             = 0;
    rstd::size_t     convolution_size_ = 0;
    bool             uses_chirp_       = false;
    Vec<rstd::usize> stage_offsets_;
    Vec<Complex32>   twiddles_;
    Vec<Complex32>   chirp_;
    Vec<Complex32>   conjugate_chirp_;
    Vec<Complex32>   kernel_spectrum_;
};

} // namespace owe::fft

export namespace rstd
{

template<>
struct Impl<Copy, owe::fft::Complex32> {};

} // namespace rstd
