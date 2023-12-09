/* ************************************************************************
 * Copyright (C) 2016-2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ************************************************************************ */

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

/* ============================================================================================ */

using hipblasNrm2BatchedExModel
    = ArgumentModel<e_a_type, e_b_type, e_compute_type, e_N, e_incx, e_batch_count>;

inline void testname_nrm2_batched_ex(const Arguments& arg, std::string& name)
{
    hipblasNrm2BatchedExModel{}.test_name(arg, name);
}

template <typename Tx, typename Tr = Tx, typename Tex = Tr>
void testing_nrm2_batched_ex_bad_arg(const Arguments& arg)
{
    bool FORTRAN                = arg.api == hipblas_client_api::FORTRAN;
    auto hipblasNrm2BatchedExFn = FORTRAN ? hipblasNrm2BatchedExFortran : hipblasNrm2BatchedEx;

    int64_t N           = 100;
    int64_t incx        = 1;
    int64_t batch_count = 2;

    hipblasDatatype_t xType         = arg.a_type;
    hipblasDatatype_t resultType    = arg.b_type;
    hipblasDatatype_t executionType = arg.compute_type;

    hipblasLocalHandle handle(arg);

    device_batch_vector<Tx> dx(N, incx, batch_count);
    device_vector<Tr>       d_res(batch_count);

    for(auto pointer_mode : {HIPBLAS_POINTER_MODE_HOST, HIPBLAS_POINTER_MODE_DEVICE})
    {
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, pointer_mode));

        // None of these test cases will write to result so using device pointer is fine for both modes
        EXPECT_HIPBLAS_STATUS(
            hipblasNrm2BatchedExFn(
                nullptr, N, dx, xType, incx, batch_count, d_res, resultType, executionType),
            HIPBLAS_STATUS_NOT_INITIALIZED);

        if(arg.bad_arg_all)
        {
            EXPECT_HIPBLAS_STATUS(
                hipblasNrm2BatchedExFn(
                    handle, N, nullptr, xType, incx, batch_count, d_res, resultType, executionType),
                HIPBLAS_STATUS_INVALID_VALUE);
            EXPECT_HIPBLAS_STATUS(
                hipblasNrm2BatchedExFn(
                    handle, N, dx, xType, incx, batch_count, nullptr, resultType, executionType),
                HIPBLAS_STATUS_INVALID_VALUE);
        }
    }
}

template <typename Tx, typename Tr = Tx, typename Tex = Tr>
void testing_nrm2_batched_ex(const Arguments& arg)
{
    bool FORTRAN                = arg.api == hipblas_client_api::FORTRAN;
    auto hipblasNrm2BatchedExFn = FORTRAN ? hipblasNrm2BatchedExFortran : hipblasNrm2BatchedEx;

    int N           = arg.N;
    int incx        = arg.incx;
    int batch_count = arg.batch_count;

    hipblasDatatype_t xType         = arg.a_type;
    hipblasDatatype_t resultType    = arg.b_type;
    hipblasDatatype_t executionType = arg.compute_type;

    hipblasLocalHandle handle(arg);

    // check to prevent undefined memory allocation error
    if(N <= 0 || incx <= 0 || batch_count <= 0)
    {
        device_vector<Tr> d_hipblas_result_0(std::max(batch_count, 1));
        host_vector<Tr>   h_hipblas_result_0(std::max(1, batch_count));
        hipblas_init_nan(h_hipblas_result_0.data(), std::max(1, batch_count));
        CHECK_HIP_ERROR(hipMemcpy(d_hipblas_result_0,
                                  h_hipblas_result_0,
                                  sizeof(Tr) * std::max(1, batch_count),
                                  hipMemcpyHostToDevice));

        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIPBLAS_ERROR(hipblasNrm2BatchedExFn(handle,
                                                   N,
                                                   nullptr,
                                                   xType,
                                                   incx,
                                                   batch_count,
                                                   d_hipblas_result_0,
                                                   resultType,
                                                   executionType));

        if(batch_count > 0)
        {
            // TODO: error in rocBLAS - only setting the first element to 0, not for all batches
            // host_vector<Tr> cpu_0(batch_count);
            // host_vector<Tr> gpu_0(batch_count);
            // CHECK_HIP_ERROR(hipMemcpy(
            //     gpu_0, d_hipblas_result_0, sizeof(Tr) * batch_count, hipMemcpyDeviceToHost));
            // unit_check_general<Tr>(1, batch_count, 1, cpu_0, gpu_0);
        }
        return;
    }

    // Naming: dX is in GPU (device) memory. hK is in CPU (host) memory, plz follow this practice
    host_batch_vector<Tx> hx(N, incx, batch_count);
    host_vector<Tr>       h_cpu_result(batch_count);
    host_vector<Tr>       h_hipblas_result_host(batch_count);
    host_vector<Tr>       h_hipblas_result_device(batch_count);

    device_batch_vector<Tx> dx(N, incx, batch_count);
    device_vector<Tr>       d_hipblas_result(batch_count);

    CHECK_HIP_ERROR(dx.memcheck());

    double gpu_time_used, hipblas_error_host, hipblas_error_device;

    // Initial Data on CPU
    hipblas_init_vector(hx, arg, hipblas_client_alpha_sets_nan, true);
    CHECK_HIP_ERROR(dx.transfer_from(hx));

    if(arg.unit_check || arg.norm_check)
    {
        // hipblasNrm2 accept both dev/host pointer for the scalar
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIPBLAS_ERROR(hipblasNrm2BatchedExFn(handle,
                                                   N,
                                                   dx.ptr_on_device(),
                                                   xType,
                                                   incx,
                                                   batch_count,
                                                   d_hipblas_result,
                                                   resultType,
                                                   executionType));

        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
        CHECK_HIPBLAS_ERROR(hipblasNrm2BatchedExFn(handle,
                                                   N,
                                                   dx.ptr_on_device(),
                                                   xType,
                                                   incx,
                                                   batch_count,
                                                   h_hipblas_result_host,
                                                   resultType,
                                                   executionType));

        CHECK_HIP_ERROR(hipMemcpy(h_hipblas_result_device,
                                  d_hipblas_result,
                                  sizeof(Tr) * batch_count,
                                  hipMemcpyDeviceToHost));

        /* =====================================================================
                    CPU BLAS
        =================================================================== */

        for(int b = 0; b < batch_count; b++)
        {
            cblas_nrm2<Tx, Tr>(N, hx[b], incx, &(h_cpu_result[b]));
        }

        double abs_result = h_cpu_result[0] > 0 ? h_cpu_result[0] : -h_cpu_result[0];
        double abs_error;

        abs_error = abs_result > 0 ? hipblas_type_epsilon<Tr> * N * abs_result
                                   : hipblas_type_epsilon<Tr> * N;

        double tolerance = 2.0; //  accounts for rounding in reduction sum. depends on n.
            //  If test fails, try decreasing n or increasing tolerance.
        abs_error *= tolerance;

        if(arg.unit_check)
        {
            near_check_general<Tr>(
                batch_count, 1, 1, h_cpu_result.data(), h_hipblas_result_host.data(), abs_error);
            near_check_general<Tr>(
                batch_count, 1, 1, h_cpu_result.data(), h_hipblas_result_device.data(), abs_error);
        }
        if(arg.norm_check)
        {
            for(int b = 0; b < batch_count; b++)
            {
                hipblas_error_host
                    = std::max(vector_norm_1(1, 1, &(h_cpu_result[b]), &(h_hipblas_result_host[b])),
                               hipblas_error_host);
                hipblas_error_device = std::max(
                    vector_norm_1(1, 1, &(h_cpu_result[b]), &(h_hipblas_result_device[b])),
                    hipblas_error_device);
            }
        }

    } // end of if unit/norm check

    if(arg.timing)
    {
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasNrm2BatchedExFn(handle,
                                                       N,
                                                       dx.ptr_on_device(),
                                                       xType,
                                                       incx,
                                                       batch_count,
                                                       d_hipblas_result,
                                                       resultType,
                                                       executionType));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        hipblasNrm2BatchedExModel{}.log_args<Tx>(std::cout,
                                                 arg,
                                                 gpu_time_used,
                                                 nrm2_gflop_count<Tx>(N),
                                                 nrm2_gbyte_count<Tx>(N),
                                                 hipblas_error_host,
                                                 hipblas_error_device);
    }
}
