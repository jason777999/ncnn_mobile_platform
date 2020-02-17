// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "batchnorm_vulkan.h"
#include <algorithm>

namespace ncnn {

DEFINE_LAYER_CREATOR(BatchNorm_vulkan)

BatchNorm_vulkan::BatchNorm_vulkan()
{
    support_vulkan = true;

    pipeline_batchnorm = 0;
    pipeline_batchnorm_pack4 = 0;
    pipeline_batchnorm_pack8 = 0;
}

int BatchNorm_vulkan::create_pipeline(const Option& opt)
{
    const Mat& shape = top_shapes.empty() ? Mat() : top_shapes[0];

    int elempack = opt.use_shader_pack8 && channels % 8 == 0 ? 8 : channels % 4 == 0 ? 4 : 1;

    Mat shape_packed;
    convert_shape_packing(shape, shape_packed, elempack);

    std::vector<vk_specialization_type> specializations(0 + 5);
    specializations[0 + 0].i = shape_packed.dims;
    specializations[0 + 1].i = shape_packed.w;
    specializations[0 + 2].i = shape_packed.h;
    specializations[0 + 3].i = shape_packed.c;
    specializations[0 + 4].i = shape_packed.cstep;

    Mat local_size_xyz(4, 4, std::min(4, channels / elempack), (void*)0);
    if (shape_packed.dims == 1)
    {
        local_size_xyz.w = std::min(64, shape_packed.w);
        local_size_xyz.h = 1;
        local_size_xyz.c = 1;
    }
    if (shape_packed.dims == 2)
    {
        local_size_xyz.w = std::min(8, shape_packed.w);
        local_size_xyz.h = std::min(8, shape_packed.h);
        local_size_xyz.c = 1;
    }
    if (shape_packed.dims == 3)
    {
        local_size_xyz.w = std::min(4, shape_packed.w);
        local_size_xyz.h = std::min(4, shape_packed.h);
        local_size_xyz.c = std::min(4, shape_packed.c);
    }

    // pack1
    if (elempack == 1)
    {
        pipeline_batchnorm = new Pipeline(vkdev);
        pipeline_batchnorm->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_batchnorm->create("batchnorm", opt, specializations, 3, 5);
    }

    // pack4
    if (elempack == 4)
    {
        pipeline_batchnorm_pack4 = new Pipeline(vkdev);
        pipeline_batchnorm_pack4->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_batchnorm_pack4->create("batchnorm_pack4", opt, specializations, 3, 5);
    }

    // pack8
    if (elempack == 8)
    {
        pipeline_batchnorm_pack8 = new Pipeline(vkdev);
        pipeline_batchnorm_pack8->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_batchnorm_pack8->create("batchnorm_pack8", opt, specializations, 3, 5);
    }

    return 0;
}

int BatchNorm_vulkan::destroy_pipeline(const Option& /*opt*/)
{
    delete pipeline_batchnorm;
    pipeline_batchnorm = 0;

    delete pipeline_batchnorm_pack4;
    pipeline_batchnorm_pack4 = 0;

    delete pipeline_batchnorm_pack8;
    pipeline_batchnorm_pack8 = 0;

    return 0;
}

int BatchNorm_vulkan::upload_model(VkTransfer& cmd, const Option& opt)
{
    cmd.record_upload(a_data, a_data_gpu, opt);

    cmd.record_upload(b_data, b_data_gpu, opt);

    return 0;
}

int BatchNorm_vulkan::forward_inplace(VkMat& bottom_top_blob, VkCompute& cmd, const Option& /*opt*/) const
{
    int elempack = bottom_top_blob.elempack;

    std::vector<VkMat> bindings(3);
    bindings[0] = bottom_top_blob;
    bindings[1] = a_data_gpu;
    bindings[2] = b_data_gpu;

    std::vector<vk_constant_type> constants(5);
    constants[0].i = bottom_top_blob.dims;
    constants[1].i = bottom_top_blob.w;
    constants[2].i = bottom_top_blob.h;
    constants[3].i = bottom_top_blob.c;
    constants[4].i = bottom_top_blob.cstep;

    const Pipeline* pipeline = elempack == 8 ? pipeline_batchnorm_pack8
                             : elempack == 4 ? pipeline_batchnorm_pack4
                             : pipeline_batchnorm;

    cmd.record_pipeline(pipeline, bindings, constants, bottom_top_blob);

    return 0;
}

} // namespace ncnn
