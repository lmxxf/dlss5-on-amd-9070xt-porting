define protected amdgpu_kernel void @vit_stream_qkv_frag_bin_w5f8_qkfrag(ptr addrspace(1) nofree noundef readonly captures(none) %0, ptr addrspace(1) nofree noundef readonly captures(none) %1, ptr addrspace(1) nofree noundef writeonly captures(none) %2, i32 noundef %3, ptr addrspace(1) nofree noundef readonly captures(address_is_null) %4) local_unnamed_addr #20 {
  %6 = icmp eq ptr addrspace(1) %4, null
  br i1 %6, label %10, label %7

7:                                                ; preds = %5
  %8 = load i32, ptr addrspace(1) %4, align 4
  %9 = icmp eq i32 %8, 0
  br i1 %9, label %10, label %744

10:                                               ; preds = %7, %5
  %11 = tail call i32 @llvm.amdgcn.workitem.id.x()
  %12 = lshr i32 %11, 5
  %13 = and i32 %11, 31
  %14 = lshr i32 %3, 4
  %15 = add nuw nsw i32 %14, 4
  %16 = udiv i32 %15, 5
  %17 = shl nuw nsw i32 %16, 5
  %18 = tail call noundef i32 @llvm.amdgcn.workgroup.id.x()
  %19 = udiv i32 %18, %17
  %20 = mul i32 %19, %17
  %21 = sub i32 %18, %20
  %22 = lshr i32 %21, 5
  %23 = mul nuw nsw i32 %22, 5
  %24 = add nuw nsw i32 %23, %12
  %25 = shl i32 %24, 4
  %26 = shl i32 %21, 5
  %27 = and i32 %26, 992
  %28 = icmp ugt i32 %19, 2
  br i1 %28, label %744, label %29

29:                                               ; preds = %10
  %30 = icmp ult i32 %25, %3
  %31 = shl nuw nsw i32 %19, 20
  %32 = zext nneg i32 %31 to i64
  %33 = getelementptr inbounds nuw i8, ptr addrspace(1) %1, i64 %32
  %34 = shl nuw nsw i32 %27, 10
  %35 = zext nneg i32 %34 to i64
  %36 = getelementptr inbounds nuw i8, ptr addrspace(1) %33, i64 %35
  %37 = icmp samesign ugt i32 %11, 127
  br i1 %37, label %38, label %41

38:                                               ; preds = %29
  %39 = shl nuw nsw i32 %11, 4
  %40 = zext nneg i32 %39 to i64
  br label %49

41:                                               ; preds = %29
  %42 = getelementptr inbounds nuw i8, ptr addrspace(1) %36, i64 16384
  %43 = shl nuw nsw i32 %11, 4
  %44 = zext nneg i32 %43 to i64
  %45 = getelementptr inbounds nuw i8, ptr addrspace(1) %42, i64 %44
  %46 = getelementptr inbounds nuw i8, ptr addrspace(1) %36, i64 %44
  %47 = load <4 x i32>, ptr addrspace(1) %46, align 1
  %48 = load <4 x i32>, ptr addrspace(1) %45, align 1
  br label %49

49:                                               ; preds = %38, %41
  %50 = phi i64 [ %40, %38 ], [ %44, %41 ]
  %51 = phi i32 [ %39, %38 ], [ %43, %41 ]
  %52 = phi <4 x i32> [ zeroinitializer, %38 ], [ %48, %41 ]
  %53 = phi <4 x i32> [ zeroinitializer, %38 ], [ %47, %41 ]
  %54 = getelementptr inbounds nuw i8, ptr addrspace(1) %36, i64 16384
  %55 = getelementptr inbounds nuw i8, ptr addrspace(1) %54, i64 %50
  %56 = getelementptr inbounds nuw i8, ptr addrspace(1) %36, i64 %50
  %57 = and i32 %11, 15
  %58 = or disjoint i32 %25, %57
  %59 = shl i32 %58, 10
  %60 = zext i32 %59 to i64
  %61 = getelementptr inbounds nuw i8, ptr addrspace(1) %0, i64 %60
  %62 = lshr i32 %11, 1
  %63 = and i32 %62, 8
  %64 = zext nneg i32 %63 to i64
  %65 = getelementptr inbounds nuw i8, ptr addrspace(1) %61, i64 %64
  %66 = shl nuw nsw i32 %13, 3
  %67 = getelementptr inbounds nuw i8, ptr addrspace(3) @_ZZ35vit_stream_qkv_frag_bin_w5f8_qkfragE3lds, i32 %66
  br label %211

68:                                               ; preds = %399
  tail call void @llvm.amdgcn.s.barrier()
  %69 = mul nuw nsw i32 %12, 2112
  %70 = getelementptr inbounds nuw i8, ptr addrspace(3) @_ZZ35vit_stream_qkv_frag_bin_w5f8_qkfragE3lds, i32 %69
  %71 = icmp eq i32 %19, 2
  br i1 %71, label %402, label %72

72:                                               ; preds = %68
  %73 = bitcast <16 x float> %400 to <2 x i256>
  %74 = getelementptr inbounds nuw [4 x i8], ptr addrspace(3) %70, i32 %57
  %75 = extractelement <2 x i256> %73, i64 0
  %76 = bitcast i256 %75 to <8 x float>
  %77 = extractelement <8 x float> %76, i64 0
  %78 = mul nuw nsw i32 %63, 132
  %79 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %78
  store float %77, ptr addrspace(3) %79, align 4
  %80 = extractelement <8 x float> %76, i64 1
  %81 = mul nuw nsw i32 %63, 132
  %82 = add nuw nsw i32 %81, 132
  %83 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %82
  store float %80, ptr addrspace(3) %83, align 4
  %84 = extractelement <8 x float> %76, i64 2
  %85 = mul nuw nsw i32 %63, 132
  %86 = add nuw nsw i32 %85, 264
  %87 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %86
  store float %84, ptr addrspace(3) %87, align 4
  %88 = extractelement <8 x float> %76, i64 3
  %89 = mul nuw nsw i32 %63, 132
  %90 = add nuw nsw i32 %89, 396
  %91 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %90
  store float %88, ptr addrspace(3) %91, align 4
  %92 = extractelement <8 x float> %76, i64 4
  %93 = mul nuw nsw i32 %63, 132
  %94 = add nuw nsw i32 %93, 528
  %95 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %94
  store float %92, ptr addrspace(3) %95, align 4
  %96 = extractelement <8 x float> %76, i64 5
  %97 = mul nuw nsw i32 %63, 132
  %98 = add nuw nsw i32 %97, 660
  %99 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %98
  store float %96, ptr addrspace(3) %99, align 4
  %100 = extractelement <8 x float> %76, i64 6
  %101 = mul nuw nsw i32 %63, 132
  %102 = add nuw nsw i32 %101, 792
  %103 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %102
  store float %100, ptr addrspace(3) %103, align 4
  %104 = extractelement <8 x float> %76, i64 7
  %105 = mul nuw nsw i32 %63, 132
  %106 = add nuw nsw i32 %105, 924
  %107 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 %106
  store float %104, ptr addrspace(3) %107, align 4
  %108 = extractelement <2 x i256> %73, i64 1
  %109 = bitcast i256 %108 to <8 x float>
  %110 = getelementptr inbounds nuw i8, ptr addrspace(3) %74, i32 64
  %111 = extractelement <8 x float> %109, i64 0
  %112 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %78
  store float %111, ptr addrspace(3) %112, align 4
  %113 = extractelement <8 x float> %109, i64 1
  %114 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %82
  store float %113, ptr addrspace(3) %114, align 4
  %115 = extractelement <8 x float> %109, i64 2
  %116 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %86
  store float %115, ptr addrspace(3) %116, align 4
  %117 = extractelement <8 x float> %109, i64 3
  %118 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %90
  store float %117, ptr addrspace(3) %118, align 4
  %119 = extractelement <8 x float> %109, i64 4
  %120 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %94
  store float %119, ptr addrspace(3) %120, align 4
  %121 = extractelement <8 x float> %109, i64 5
  %122 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %98
  store float %121, ptr addrspace(3) %122, align 4
  %123 = extractelement <8 x float> %109, i64 6
  %124 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %102
  store float %123, ptr addrspace(3) %124, align 4
  %125 = extractelement <8 x float> %109, i64 7
  %126 = getelementptr inbounds nuw i8, ptr addrspace(3) %110, i32 %106
  store float %125, ptr addrspace(3) %126, align 4
  tail call void @llvm.amdgcn.s.barrier()
  %127 = mul nuw nsw i32 %57, 132
  %128 = getelementptr inbounds nuw i8, ptr addrspace(3) %70, i32 %127
  %129 = getelementptr inbounds nuw [4 x i8], ptr addrspace(3) %128, i32 %63
  %130 = load float, ptr addrspace(3) %129, align 4
  %131 = fmul float %130, %130
  %132 = fptrunc float %131 to half
  %133 = insertelement <8 x half> poison, half %132, i64 0
  %134 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 4
  %135 = load float, ptr addrspace(3) %134, align 4
  %136 = fmul float %135, %135
  %137 = fptrunc float %136 to half
  %138 = insertelement <8 x half> %133, half %137, i64 1
  %139 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 8
  %140 = load float, ptr addrspace(3) %139, align 4
  %141 = fmul float %140, %140
  %142 = fptrunc float %141 to half
  %143 = insertelement <8 x half> %138, half %142, i64 2
  %144 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 12
  %145 = load float, ptr addrspace(3) %144, align 4
  %146 = fmul float %145, %145
  %147 = fptrunc float %146 to half
  %148 = insertelement <8 x half> %143, half %147, i64 3
  %149 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 16
  %150 = load float, ptr addrspace(3) %149, align 4
  %151 = fmul float %150, %150
  %152 = fptrunc float %151 to half
  %153 = insertelement <8 x half> %148, half %152, i64 4
  %154 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 20
  %155 = load float, ptr addrspace(3) %154, align 4
  %156 = fmul float %155, %155
  %157 = fptrunc float %156 to half
  %158 = insertelement <8 x half> %153, half %157, i64 5
  %159 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 24
  %160 = load float, ptr addrspace(3) %159, align 4
  %161 = fmul float %160, %160
  %162 = fptrunc float %161 to half
  %163 = insertelement <8 x half> %158, half %162, i64 6
  %164 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 28
  %165 = load float, ptr addrspace(3) %164, align 4
  %166 = fmul float %165, %165
  %167 = fptrunc float %166 to half
  %168 = insertelement <8 x half> %163, half %167, i64 7
  %169 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.f16.v8f32.v8f16(<8 x half> %168, <8 x half> splat (half 1.000000e+00), <8 x float> zeroinitializer)
  %170 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 64
  %171 = load float, ptr addrspace(3) %170, align 4
  %172 = fmul float %171, %171
  %173 = fptrunc float %172 to half
  %174 = insertelement <8 x half> poison, half %173, i64 0
  %175 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 68
  %176 = load float, ptr addrspace(3) %175, align 4
  %177 = fmul float %176, %176
  %178 = fptrunc float %177 to half
  %179 = insertelement <8 x half> %174, half %178, i64 1
  %180 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 72
  %181 = load float, ptr addrspace(3) %180, align 4
  %182 = fmul float %181, %181
  %183 = fptrunc float %182 to half
  %184 = insertelement <8 x half> %179, half %183, i64 2
  %185 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 76
  %186 = load float, ptr addrspace(3) %185, align 4
  %187 = fmul float %186, %186
  %188 = fptrunc float %187 to half
  %189 = insertelement <8 x half> %184, half %188, i64 3
  %190 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 80
  %191 = load float, ptr addrspace(3) %190, align 4
  %192 = fmul float %191, %191
  %193 = fptrunc float %192 to half
  %194 = insertelement <8 x half> %189, half %193, i64 4
  %195 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 84
  %196 = load float, ptr addrspace(3) %195, align 4
  %197 = fmul float %196, %196
  %198 = fptrunc float %197 to half
  %199 = insertelement <8 x half> %194, half %198, i64 5
  %200 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 88
  %201 = load float, ptr addrspace(3) %200, align 4
  %202 = fmul float %201, %201
  %203 = fptrunc float %202 to half
  %204 = insertelement <8 x half> %199, half %203, i64 6
  %205 = getelementptr inbounds nuw i8, ptr addrspace(3) %129, i32 92
  %206 = load float, ptr addrspace(3) %205, align 4
  %207 = fmul float %206, %206
  %208 = fptrunc float %207 to half
  %209 = insertelement <8 x half> %204, half %208, i64 7
  %210 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.f16.v8f32.v8f16(<8 x half> %209, <8 x half> splat (half 1.000000e+00), <8 x float> %169)
  br label %402

211:                                              ; preds = %49, %399
  %212 = phi <4 x i32> [ %53, %49 ], [ %236, %399 ]
  %213 = phi <4 x i32> [ %52, %49 ], [ %235, %399 ]
  %214 = phi i32 [ 0, %49 ], [ %224, %399 ]
  %215 = phi <16 x float> [ zeroinitializer, %49 ], [ %400, %399 ]
  %216 = and i32 %214, 1
  br i1 %37, label %223, label %217

217:                                              ; preds = %211
  %218 = shl nuw nsw i32 %216, 12
  %219 = getelementptr inbounds nuw i8, ptr addrspace(3) @_ZZ35vit_stream_qkv_frag_bin_w5f8_qkfragE3lds, i32 %218
  %220 = getelementptr inbounds nuw i8, ptr addrspace(3) %219, i32 2048
  %221 = getelementptr inbounds nuw i8, ptr addrspace(3) %220, i32 %51
  %222 = getelementptr inbounds nuw i8, ptr addrspace(3) %219, i32 %51
  store <4 x i32> %212, ptr addrspace(3) %222, align 2
  store <4 x i32> %213, ptr addrspace(3) %221, align 2
  br label %223

223:                                              ; preds = %217, %211
  tail call void @llvm.amdgcn.s.barrier()
  %224 = add nuw nsw i32 %214, 1
  %225 = icmp eq i32 %214, 7
  %226 = or i1 %37, %225
  br i1 %226, label %234, label %227

227:                                              ; preds = %223
  %228 = shl nuw nsw i32 %224, 11
  %229 = zext nneg i32 %228 to i64
  %230 = getelementptr inbounds nuw i8, ptr addrspace(1) %55, i64 %229
  %231 = getelementptr inbounds nuw i8, ptr addrspace(1) %56, i64 %229
  %232 = load <4 x i32>, ptr addrspace(1) %231, align 1
  %233 = load <4 x i32>, ptr addrspace(1) %230, align 1
  br label %234

234:                                              ; preds = %223, %227
  %235 = phi <4 x i32> [ %213, %223 ], [ %233, %227 ]
  %236 = phi <4 x i32> [ %212, %223 ], [ %232, %227 ]
  br i1 %30, label %237, label %399

237:                                              ; preds = %234
  %238 = shl nuw nsw i32 %214, 7
  %239 = shl nuw nsw i32 %216, 12
  %240 = getelementptr inbounds nuw i8, ptr addrspace(3) %67, i32 %239
  %241 = zext nneg i32 %238 to i64
  %242 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %241
  %243 = load <2 x i32>, ptr addrspace(1) %242, align 1
  %244 = load <2 x i32>, ptr addrspace(3) %240, align 2
  %245 = bitcast <16 x float> %215 to <2 x i256>
  %246 = extractelement <2 x i256> %245, i64 0
  %247 = bitcast i256 %246 to <8 x float>
  %248 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %243, <2 x i32> %244, <8 x float> %247)
  %249 = shufflevector <8 x float> %248, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %250 = shufflevector <16 x float> %249, <16 x float> %215, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %251 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 2048
  %252 = load <2 x i32>, ptr addrspace(3) %251, align 2
  %253 = bitcast <16 x float> %250 to <2 x i256>
  %254 = extractelement <2 x i256> %253, i64 1
  %255 = bitcast i256 %254 to <8 x float>
  %256 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %243, <2 x i32> %252, <8 x float> %255)
  %257 = shufflevector <8 x float> %256, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %258 = shufflevector <16 x float> %250, <16 x float> %257, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %259 = zext nneg i32 %238 to i64
  %260 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %259
  %261 = getelementptr inbounds nuw i8, ptr addrspace(1) %260, i64 16
  %262 = load <2 x i32>, ptr addrspace(1) %261, align 1
  %263 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 256
  %264 = load <2 x i32>, ptr addrspace(3) %263, align 2
  %265 = bitcast <16 x float> %258 to <2 x i256>
  %266 = extractelement <2 x i256> %265, i64 0
  %267 = bitcast i256 %266 to <8 x float>
  %268 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %262, <2 x i32> %264, <8 x float> %267)
  %269 = shufflevector <8 x float> %268, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %270 = shufflevector <16 x float> %269, <16 x float> %258, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %271 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 2304
  %272 = load <2 x i32>, ptr addrspace(3) %271, align 2
  %273 = bitcast <16 x float> %270 to <2 x i256>
  %274 = extractelement <2 x i256> %273, i64 1
  %275 = bitcast i256 %274 to <8 x float>
  %276 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %262, <2 x i32> %272, <8 x float> %275)
  %277 = shufflevector <8 x float> %276, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %278 = shufflevector <16 x float> %270, <16 x float> %277, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %279 = zext nneg i32 %238 to i64
  %280 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %279
  %281 = getelementptr inbounds nuw i8, ptr addrspace(1) %280, i64 32
  %282 = load <2 x i32>, ptr addrspace(1) %281, align 1
  %283 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 512
  %284 = load <2 x i32>, ptr addrspace(3) %283, align 2
  %285 = bitcast <16 x float> %278 to <2 x i256>
  %286 = extractelement <2 x i256> %285, i64 0
  %287 = bitcast i256 %286 to <8 x float>
  %288 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %282, <2 x i32> %284, <8 x float> %287)
  %289 = shufflevector <8 x float> %288, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %290 = shufflevector <16 x float> %289, <16 x float> %278, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %291 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 2560
  %292 = load <2 x i32>, ptr addrspace(3) %291, align 2
  %293 = bitcast <16 x float> %290 to <2 x i256>
  %294 = extractelement <2 x i256> %293, i64 1
  %295 = bitcast i256 %294 to <8 x float>
  %296 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %282, <2 x i32> %292, <8 x float> %295)
  %297 = shufflevector <8 x float> %296, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %298 = shufflevector <16 x float> %290, <16 x float> %297, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %299 = zext nneg i32 %238 to i64
  %300 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %299
  %301 = getelementptr inbounds nuw i8, ptr addrspace(1) %300, i64 48
  %302 = load <2 x i32>, ptr addrspace(1) %301, align 1
  %303 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 768
  %304 = load <2 x i32>, ptr addrspace(3) %303, align 2
  %305 = bitcast <16 x float> %298 to <2 x i256>
  %306 = extractelement <2 x i256> %305, i64 0
  %307 = bitcast i256 %306 to <8 x float>
  %308 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %302, <2 x i32> %304, <8 x float> %307)
  %309 = shufflevector <8 x float> %308, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %310 = shufflevector <16 x float> %309, <16 x float> %298, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %311 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 2816
  %312 = load <2 x i32>, ptr addrspace(3) %311, align 2
  %313 = bitcast <16 x float> %310 to <2 x i256>
  %314 = extractelement <2 x i256> %313, i64 1
  %315 = bitcast i256 %314 to <8 x float>
  %316 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %302, <2 x i32> %312, <8 x float> %315)
  %317 = shufflevector <8 x float> %316, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %318 = shufflevector <16 x float> %310, <16 x float> %317, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %319 = zext nneg i32 %238 to i64
  %320 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %319
  %321 = getelementptr inbounds nuw i8, ptr addrspace(1) %320, i64 64
  %322 = load <2 x i32>, ptr addrspace(1) %321, align 1
  %323 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 1024
  %324 = load <2 x i32>, ptr addrspace(3) %323, align 2
  %325 = bitcast <16 x float> %318 to <2 x i256>
  %326 = extractelement <2 x i256> %325, i64 0
  %327 = bitcast i256 %326 to <8 x float>
  %328 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %322, <2 x i32> %324, <8 x float> %327)
  %329 = shufflevector <8 x float> %328, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %330 = shufflevector <16 x float> %329, <16 x float> %318, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %331 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 3072
  %332 = load <2 x i32>, ptr addrspace(3) %331, align 2
  %333 = bitcast <16 x float> %330 to <2 x i256>
  %334 = extractelement <2 x i256> %333, i64 1
  %335 = bitcast i256 %334 to <8 x float>
  %336 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %322, <2 x i32> %332, <8 x float> %335)
  %337 = shufflevector <8 x float> %336, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %338 = shufflevector <16 x float> %330, <16 x float> %337, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %339 = zext nneg i32 %238 to i64
  %340 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %339
  %341 = getelementptr inbounds nuw i8, ptr addrspace(1) %340, i64 80
  %342 = load <2 x i32>, ptr addrspace(1) %341, align 1
  %343 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 1280
  %344 = load <2 x i32>, ptr addrspace(3) %343, align 2
  %345 = bitcast <16 x float> %338 to <2 x i256>
  %346 = extractelement <2 x i256> %345, i64 0
  %347 = bitcast i256 %346 to <8 x float>
  %348 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %342, <2 x i32> %344, <8 x float> %347)
  %349 = shufflevector <8 x float> %348, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %350 = shufflevector <16 x float> %349, <16 x float> %338, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %351 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 3328
  %352 = load <2 x i32>, ptr addrspace(3) %351, align 2
  %353 = bitcast <16 x float> %350 to <2 x i256>
  %354 = extractelement <2 x i256> %353, i64 1
  %355 = bitcast i256 %354 to <8 x float>
  %356 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %342, <2 x i32> %352, <8 x float> %355)
  %357 = shufflevector <8 x float> %356, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %358 = shufflevector <16 x float> %350, <16 x float> %357, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %359 = zext nneg i32 %238 to i64
  %360 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %359
  %361 = getelementptr inbounds nuw i8, ptr addrspace(1) %360, i64 96
  %362 = load <2 x i32>, ptr addrspace(1) %361, align 1
  %363 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 1536
  %364 = load <2 x i32>, ptr addrspace(3) %363, align 2
  %365 = bitcast <16 x float> %358 to <2 x i256>
  %366 = extractelement <2 x i256> %365, i64 0
  %367 = bitcast i256 %366 to <8 x float>
  %368 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %362, <2 x i32> %364, <8 x float> %367)
  %369 = shufflevector <8 x float> %368, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %370 = shufflevector <16 x float> %369, <16 x float> %358, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %371 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 3584
  %372 = load <2 x i32>, ptr addrspace(3) %371, align 2
  %373 = bitcast <16 x float> %370 to <2 x i256>
  %374 = extractelement <2 x i256> %373, i64 1
  %375 = bitcast i256 %374 to <8 x float>
  %376 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %362, <2 x i32> %372, <8 x float> %375)
  %377 = shufflevector <8 x float> %376, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %378 = shufflevector <16 x float> %370, <16 x float> %377, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  %379 = zext nneg i32 %238 to i64
  %380 = getelementptr inbounds nuw i8, ptr addrspace(1) %65, i64 %379
  %381 = getelementptr inbounds nuw i8, ptr addrspace(1) %380, i64 112
  %382 = load <2 x i32>, ptr addrspace(1) %381, align 1
  %383 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 1792
  %384 = load <2 x i32>, ptr addrspace(3) %383, align 2
  %385 = bitcast <16 x float> %378 to <2 x i256>
  %386 = extractelement <2 x i256> %385, i64 0
  %387 = bitcast i256 %386 to <8 x float>
  %388 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %382, <2 x i32> %384, <8 x float> %387)
  %389 = shufflevector <8 x float> %388, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %390 = shufflevector <16 x float> %389, <16 x float> %378, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %391 = getelementptr inbounds nuw i8, ptr addrspace(3) %240, i32 3840
  %392 = load <2 x i32>, ptr addrspace(3) %391, align 2
  %393 = bitcast <16 x float> %390 to <2 x i256>
  %394 = extractelement <2 x i256> %393, i64 1
  %395 = bitcast i256 %394 to <8 x float>
  %396 = tail call contract <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8.v8f32.v2i32(<2 x i32> %382, <2 x i32> %392, <8 x float> %395)
  %397 = shufflevector <8 x float> %396, <8 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison>
  %398 = shufflevector <16 x float> %390, <16 x float> %397, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23>
  br label %399

399:                                              ; preds = %237, %234
  %400 = phi <16 x float> [ %215, %234 ], [ %398, %237 ]
  %401 = icmp eq i32 %224, 8
  br i1 %401, label %68, label %211, !llvm.loop !133

402:                                              ; preds = %72, %68
  %403 = phi <8 x float> [ zeroinitializer, %68 ], [ %210, %72 ]
  br i1 %30, label %404, label %744

404:                                              ; preds = %402
  br i1 %71, label %497, label %405

405:                                              ; preds = %404
  %406 = icmp ugt i32 %17, %18
  br i1 %406, label %407, label %414

407:                                              ; preds = %405
  %408 = lshr exact i32 %27, 5
  %409 = zext nneg i32 %408 to i64
  %410 = getelementptr inbounds nuw [4 x i8], ptr addrspace(1) %1, i64 %409
  %411 = getelementptr inbounds nuw i8, ptr addrspace(1) %410, i64 3145728
  %412 = load float, ptr addrspace(1) %411, align 4
  %413 = fmul float %412, 5.656250e+00
  br label %414

414:                                              ; preds = %405, %407
  %415 = phi contract float [ %413, %407 ], [ 1.000000e+00, %405 ]
  %416 = extractelement <8 x float> %403, i64 0
  %417 = tail call float @llvm.maxnum.f32(float %416, float f0x38820000)
  %418 = tail call contract float @llvm.amdgcn.rsq.f32(float %417)
  %419 = fmul float %415, %418
  %420 = extractelement <16 x float> %400, i64 0
  %421 = fmul float %420, %419
  %422 = insertelement <8 x float> poison, float %421, i64 0
  %423 = extractelement <16 x float> %400, i64 8
  %424 = fmul float %423, %419
  %425 = insertelement <8 x float> poison, float %424, i64 0
  %426 = extractelement <8 x float> %403, i64 1
  %427 = tail call float @llvm.maxnum.f32(float %426, float f0x38820000)
  %428 = tail call contract float @llvm.amdgcn.rsq.f32(float %427)
  %429 = fmul float %415, %428
  %430 = extractelement <16 x float> %400, i64 1
  %431 = fmul float %430, %429
  %432 = insertelement <8 x float> %422, float %431, i64 1
  %433 = extractelement <16 x float> %400, i64 9
  %434 = fmul float %433, %429
  %435 = insertelement <8 x float> %425, float %434, i64 1
  %436 = extractelement <8 x float> %403, i64 2
  %437 = tail call float @llvm.maxnum.f32(float %436, float f0x38820000)
  %438 = tail call contract float @llvm.amdgcn.rsq.f32(float %437)
  %439 = fmul float %415, %438
  %440 = extractelement <16 x float> %400, i64 2
  %441 = fmul float %440, %439
  %442 = insertelement <8 x float> %432, float %441, i64 2
  %443 = extractelement <16 x float> %400, i64 10
  %444 = fmul float %443, %439
  %445 = insertelement <8 x float> %435, float %444, i64 2
  %446 = extractelement <8 x float> %403, i64 3
  %447 = tail call float @llvm.maxnum.f32(float %446, float f0x38820000)
  %448 = tail call contract float @llvm.amdgcn.rsq.f32(float %447)
  %449 = fmul float %415, %448
  %450 = extractelement <16 x float> %400, i64 3
  %451 = fmul float %450, %449
  %452 = insertelement <8 x float> %442, float %451, i64 3
  %453 = extractelement <16 x float> %400, i64 11
  %454 = fmul float %453, %449
  %455 = insertelement <8 x float> %445, float %454, i64 3
  %456 = extractelement <8 x float> %403, i64 4
  %457 = tail call float @llvm.maxnum.f32(float %456, float f0x38820000)
  %458 = tail call contract float @llvm.amdgcn.rsq.f32(float %457)
  %459 = fmul float %415, %458
  %460 = extractelement <16 x float> %400, i64 4
  %461 = fmul float %460, %459
  %462 = insertelement <8 x float> %452, float %461, i64 4
  %463 = extractelement <16 x float> %400, i64 12
  %464 = fmul float %463, %459
  %465 = insertelement <8 x float> %455, float %464, i64 4
  %466 = extractelement <8 x float> %403, i64 5
  %467 = tail call float @llvm.maxnum.f32(float %466, float f0x38820000)
  %468 = tail call contract float @llvm.amdgcn.rsq.f32(float %467)
  %469 = fmul float %415, %468
  %470 = extractelement <16 x float> %400, i64 5
  %471 = fmul float %470, %469
  %472 = insertelement <8 x float> %462, float %471, i64 5
  %473 = extractelement <16 x float> %400, i64 13
  %474 = fmul float %473, %469
  %475 = insertelement <8 x float> %465, float %474, i64 5
  %476 = extractelement <8 x float> %403, i64 6
  %477 = tail call float @llvm.maxnum.f32(float %476, float f0x38820000)
  %478 = tail call contract float @llvm.amdgcn.rsq.f32(float %477)
  %479 = fmul float %415, %478
  %480 = extractelement <16 x float> %400, i64 6
  %481 = fmul float %480, %479
  %482 = insertelement <8 x float> %472, float %481, i64 6
  %483 = extractelement <16 x float> %400, i64 14
  %484 = fmul float %483, %479
  %485 = insertelement <8 x float> %475, float %484, i64 6
  %486 = extractelement <8 x float> %403, i64 7
  %487 = tail call float @llvm.maxnum.f32(float %486, float f0x38820000)
  %488 = tail call contract float @llvm.amdgcn.rsq.f32(float %487)
  %489 = fmul float %415, %488
  %490 = extractelement <16 x float> %400, i64 7
  %491 = fmul float %490, %489
  %492 = insertelement <8 x float> %482, float %491, i64 7
  %493 = extractelement <16 x float> %400, i64 15
  %494 = fmul float %493, %489
  %495 = insertelement <8 x float> %485, float %494, i64 7
  %496 = shufflevector <8 x float> %492, <8 x float> %495, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  br label %497

497:                                              ; preds = %414, %404
  %498 = phi <16 x float> [ %400, %404 ], [ %496, %414 ]
  br i1 %71, label %499, label %611

499:                                              ; preds = %497
  %500 = bitcast <16 x float> %498 to <2 x i256>
  %501 = extractelement <2 x i256> %500, i64 0
  %502 = bitcast i256 %501 to <8 x float>
  %503 = extractelement <8 x float> %502, i64 0
  %504 = fadd float %503, 0.000000e+00
  %505 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %504, float -4.480000e+02, float 4.480000e+02)
  %506 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %505, float 0.000000e+00, i32 0, i1 false)
  %507 = and i32 %506, 255
  %508 = extractelement <8 x float> %502, i64 1
  %509 = fadd float %508, 0.000000e+00
  %510 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %509, float -4.480000e+02, float 4.480000e+02)
  %511 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %510, float 0.000000e+00, i32 0, i1 false)
  %512 = shl i32 %511, 8
  %513 = and i32 %512, 65280
  %514 = or disjoint i32 %513, %507
  %515 = extractelement <8 x float> %502, i64 2
  %516 = fadd float %515, 0.000000e+00
  %517 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %516, float -4.480000e+02, float 4.480000e+02)
  %518 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %517, float 0.000000e+00, i32 0, i1 false)
  %519 = shl i32 %518, 16
  %520 = and i32 %519, 16711680
  %521 = or disjoint i32 %520, %514
  %522 = extractelement <8 x float> %502, i64 3
  %523 = fadd float %522, 0.000000e+00
  %524 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %523, float -4.480000e+02, float 4.480000e+02)
  %525 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %524, float 0.000000e+00, i32 0, i1 false)
  %526 = shl i32 %525, 24
  %527 = or disjoint i32 %526, %521
  %528 = insertelement <4 x i32> poison, i32 %527, i64 0
  %529 = extractelement <8 x float> %502, i64 4
  %530 = fadd float %529, 0.000000e+00
  %531 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %530, float -4.480000e+02, float 4.480000e+02)
  %532 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %531, float 0.000000e+00, i32 0, i1 false)
  %533 = and i32 %532, 255
  %534 = extractelement <8 x float> %502, i64 5
  %535 = fadd float %534, 0.000000e+00
  %536 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %535, float -4.480000e+02, float 4.480000e+02)
  %537 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %536, float 0.000000e+00, i32 0, i1 false)
  %538 = shl i32 %537, 8
  %539 = and i32 %538, 65280
  %540 = or disjoint i32 %539, %533
  %541 = extractelement <8 x float> %502, i64 6
  %542 = fadd float %541, 0.000000e+00
  %543 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %542, float -4.480000e+02, float 4.480000e+02)
  %544 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %543, float 0.000000e+00, i32 0, i1 false)
  %545 = shl i32 %544, 16
  %546 = and i32 %545, 16711680
  %547 = or disjoint i32 %546, %540
  %548 = extractelement <8 x float> %502, i64 7
  %549 = fadd float %548, 0.000000e+00
  %550 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %549, float -4.480000e+02, float 4.480000e+02)
  %551 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %550, float 0.000000e+00, i32 0, i1 false)
  %552 = shl i32 %551, 24
  %553 = or disjoint i32 %552, %547
  %554 = insertelement <4 x i32> %528, i32 %553, i64 1
  %555 = extractelement <2 x i256> %500, i64 1
  %556 = bitcast i256 %555 to <8 x float>
  %557 = extractelement <8 x float> %556, i64 0
  %558 = fadd float %557, 0.000000e+00
  %559 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %558, float -4.480000e+02, float 4.480000e+02)
  %560 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %559, float 0.000000e+00, i32 0, i1 false)
  %561 = and i32 %560, 255
  %562 = extractelement <8 x float> %556, i64 1
  %563 = fadd float %562, 0.000000e+00
  %564 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %563, float -4.480000e+02, float 4.480000e+02)
  %565 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %564, float 0.000000e+00, i32 0, i1 false)
  %566 = shl i32 %565, 8
  %567 = and i32 %566, 65280
  %568 = or disjoint i32 %567, %561
  %569 = extractelement <8 x float> %556, i64 2
  %570 = fadd float %569, 0.000000e+00
  %571 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %570, float -4.480000e+02, float 4.480000e+02)
  %572 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %571, float 0.000000e+00, i32 0, i1 false)
  %573 = shl i32 %572, 16
  %574 = and i32 %573, 16711680
  %575 = or disjoint i32 %574, %568
  %576 = extractelement <8 x float> %556, i64 3
  %577 = fadd float %576, 0.000000e+00
  %578 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %577, float -4.480000e+02, float 4.480000e+02)
  %579 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %578, float 0.000000e+00, i32 0, i1 false)
  %580 = shl i32 %579, 24
  %581 = or disjoint i32 %580, %575
  %582 = insertelement <4 x i32> %554, i32 %581, i64 2
  %583 = extractelement <8 x float> %556, i64 4
  %584 = fadd float %583, 0.000000e+00
  %585 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %584, float -4.480000e+02, float 4.480000e+02)
  %586 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %585, float 0.000000e+00, i32 0, i1 false)
  %587 = and i32 %586, 255
  %588 = extractelement <8 x float> %556, i64 5
  %589 = fadd float %588, 0.000000e+00
  %590 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %589, float -4.480000e+02, float 4.480000e+02)
  %591 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %590, float 0.000000e+00, i32 0, i1 false)
  %592 = shl i32 %591, 8
  %593 = and i32 %592, 65280
  %594 = or disjoint i32 %593, %587
  %595 = extractelement <8 x float> %556, i64 6
  %596 = fadd float %595, 0.000000e+00
  %597 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %596, float -4.480000e+02, float 4.480000e+02)
  %598 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %597, float 0.000000e+00, i32 0, i1 false)
  %599 = shl i32 %598, 16
  %600 = and i32 %599, 16711680
  %601 = or disjoint i32 %600, %594
  %602 = extractelement <8 x float> %556, i64 7
  %603 = fadd float %602, 0.000000e+00
  %604 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %603, float -4.480000e+02, float 4.480000e+02)
  %605 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %604, float 0.000000e+00, i32 0, i1 false)
  %606 = shl i32 %605, 24
  %607 = or disjoint i32 %606, %601
  %608 = insertelement <4 x i32> %582, i32 %607, i64 3
  %609 = zext i32 %3 to i64
  %610 = shl nuw nsw i64 %609, 11
  br label %728

611:                                              ; preds = %497
  fence syncscope("workgroup") release, !mmra !13
  tail call void @llvm.amdgcn.wave.barrier()
  fence syncscope("workgroup") acquire, !mmra !13
  %612 = bitcast <16 x float> %498 to <2 x i256>
  %613 = and i32 %51, 256
  %614 = getelementptr inbounds nuw i8, ptr addrspace(3) %70, i32 %613
  %615 = getelementptr inbounds nuw i8, ptr addrspace(3) %614, i32 %57
  %616 = extractelement <2 x i256> %612, i64 0
  %617 = bitcast i256 %616 to <8 x float>
  %618 = extractelement <8 x float> %617, i64 0
  %619 = fadd float %618, 0.000000e+00
  %620 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %619, float -4.480000e+02, float 4.480000e+02)
  %621 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %620, float 0.000000e+00, i32 0, i1 false)
  %622 = trunc i32 %621 to i8
  store i8 %622, ptr addrspace(3) %615, align 1
  %623 = extractelement <8 x float> %617, i64 1
  %624 = fadd float %623, 0.000000e+00
  %625 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %624, float -4.480000e+02, float 4.480000e+02)
  %626 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %625, float 0.000000e+00, i32 0, i1 false)
  %627 = trunc i32 %626 to i8
  %628 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 32
  store i8 %627, ptr addrspace(3) %628, align 1
  %629 = extractelement <8 x float> %617, i64 2
  %630 = fadd float %629, 0.000000e+00
  %631 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %630, float -4.480000e+02, float 4.480000e+02)
  %632 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %631, float 0.000000e+00, i32 0, i1 false)
  %633 = trunc i32 %632 to i8
  %634 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 64
  store i8 %633, ptr addrspace(3) %634, align 1
  %635 = extractelement <8 x float> %617, i64 3
  %636 = fadd float %635, 0.000000e+00
  %637 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %636, float -4.480000e+02, float 4.480000e+02)
  %638 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %637, float 0.000000e+00, i32 0, i1 false)
  %639 = trunc i32 %638 to i8
  %640 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 96
  store i8 %639, ptr addrspace(3) %640, align 1
  %641 = extractelement <8 x float> %617, i64 4
  %642 = fadd float %641, 0.000000e+00
  %643 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %642, float -4.480000e+02, float 4.480000e+02)
  %644 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %643, float 0.000000e+00, i32 0, i1 false)
  %645 = trunc i32 %644 to i8
  %646 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 128
  store i8 %645, ptr addrspace(3) %646, align 1
  %647 = extractelement <8 x float> %617, i64 5
  %648 = fadd float %647, 0.000000e+00
  %649 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %648, float -4.480000e+02, float 4.480000e+02)
  %650 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %649, float 0.000000e+00, i32 0, i1 false)
  %651 = trunc i32 %650 to i8
  %652 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 160
  store i8 %651, ptr addrspace(3) %652, align 1
  %653 = extractelement <8 x float> %617, i64 6
  %654 = fadd float %653, 0.000000e+00
  %655 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %654, float -4.480000e+02, float 4.480000e+02)
  %656 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %655, float 0.000000e+00, i32 0, i1 false)
  %657 = trunc i32 %656 to i8
  %658 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 192
  store i8 %657, ptr addrspace(3) %658, align 1
  %659 = extractelement <8 x float> %617, i64 7
  %660 = fadd float %659, 0.000000e+00
  %661 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %660, float -4.480000e+02, float 4.480000e+02)
  %662 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %661, float 0.000000e+00, i32 0, i1 false)
  %663 = trunc i32 %662 to i8
  %664 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 224
  store i8 %663, ptr addrspace(3) %664, align 1
  %665 = extractelement <2 x i256> %612, i64 1
  %666 = bitcast i256 %665 to <8 x float>
  %667 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 16
  %668 = extractelement <8 x float> %666, i64 0
  %669 = fadd float %668, 0.000000e+00
  %670 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %669, float -4.480000e+02, float 4.480000e+02)
  %671 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %670, float 0.000000e+00, i32 0, i1 false)
  %672 = trunc i32 %671 to i8
  store i8 %672, ptr addrspace(3) %667, align 1
  %673 = extractelement <8 x float> %666, i64 1
  %674 = fadd float %673, 0.000000e+00
  %675 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %674, float -4.480000e+02, float 4.480000e+02)
  %676 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %675, float 0.000000e+00, i32 0, i1 false)
  %677 = trunc i32 %676 to i8
  %678 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 48
  store i8 %677, ptr addrspace(3) %678, align 1
  %679 = extractelement <8 x float> %666, i64 2
  %680 = fadd float %679, 0.000000e+00
  %681 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %680, float -4.480000e+02, float 4.480000e+02)
  %682 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %681, float 0.000000e+00, i32 0, i1 false)
  %683 = trunc i32 %682 to i8
  %684 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 80
  store i8 %683, ptr addrspace(3) %684, align 1
  %685 = extractelement <8 x float> %666, i64 3
  %686 = fadd float %685, 0.000000e+00
  %687 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %686, float -4.480000e+02, float 4.480000e+02)
  %688 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %687, float 0.000000e+00, i32 0, i1 false)
  %689 = trunc i32 %688 to i8
  %690 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 112
  store i8 %689, ptr addrspace(3) %690, align 1
  %691 = extractelement <8 x float> %666, i64 4
  %692 = fadd float %691, 0.000000e+00
  %693 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %692, float -4.480000e+02, float 4.480000e+02)
  %694 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %693, float 0.000000e+00, i32 0, i1 false)
  %695 = trunc i32 %694 to i8
  %696 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 144
  store i8 %695, ptr addrspace(3) %696, align 1
  %697 = extractelement <8 x float> %666, i64 5
  %698 = fadd float %697, 0.000000e+00
  %699 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %698, float -4.480000e+02, float 4.480000e+02)
  %700 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %699, float 0.000000e+00, i32 0, i1 false)
  %701 = trunc i32 %700 to i8
  %702 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 176
  store i8 %701, ptr addrspace(3) %702, align 1
  %703 = extractelement <8 x float> %666, i64 6
  %704 = fadd float %703, 0.000000e+00
  %705 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %704, float -4.480000e+02, float 4.480000e+02)
  %706 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %705, float 0.000000e+00, i32 0, i1 false)
  %707 = trunc i32 %706 to i8
  %708 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 208
  store i8 %707, ptr addrspace(3) %708, align 1
  %709 = extractelement <8 x float> %666, i64 7
  %710 = fadd float %709, 0.000000e+00
  %711 = tail call contract noundef float @llvm.amdgcn.fmed3.f32(float %710, float -4.480000e+02, float 4.480000e+02)
  %712 = tail call i32 @llvm.amdgcn.cvt.pk.fp8.f32(float %711, float 0.000000e+00, i32 0, i1 false)
  %713 = trunc i32 %712 to i8
  %714 = getelementptr inbounds nuw i8, ptr addrspace(3) %615, i32 240
  store i8 %713, ptr addrspace(3) %714, align 1
  fence syncscope("workgroup") release, !mmra !13
  tail call void @llvm.amdgcn.wave.barrier()
  fence syncscope("workgroup") acquire, !mmra !13
  %715 = shl nuw nsw i32 %11, 5
  %716 = and i32 %715, 480
  %717 = getelementptr inbounds nuw i8, ptr addrspace(3) %70, i32 %716
  %718 = getelementptr inbounds nuw i8, ptr addrspace(3) %717, i32 %63
  %719 = load <2 x i32>, ptr addrspace(3) %718, align 2
  %720 = getelementptr inbounds nuw i8, ptr addrspace(3) %717, i32 16
  %721 = getelementptr inbounds nuw i8, ptr addrspace(3) %720, i32 %63
  %722 = load <2 x i32>, ptr addrspace(3) %721, align 2
  %723 = shufflevector <2 x i32> %719, <2 x i32> %722, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %724 = zext nneg i32 %19 to i64
  %725 = zext i32 %3 to i64
  %726 = shl nuw nsw i64 %725, 10
  %727 = mul nuw nsw i64 %726, %724
  br label %728

728:                                              ; preds = %499, %611
  %729 = phi i64 [ %727, %611 ], [ %610, %499 ]
  %730 = phi <4 x i32> [ %723, %611 ], [ %608, %499 ]
  %731 = lshr exact i32 %27, 5
  %732 = zext nneg i32 %731 to i64
  %733 = zext nneg i32 %14 to i64
  %734 = mul nuw nsw i64 %732, %733
  %735 = and i32 %24, 268435455
  %736 = zext nneg i32 %735 to i64
  %737 = add nuw nsw i64 %734, %736
  %738 = shl nuw nsw i64 %737, 9
  %739 = shl nuw nsw i32 %13, 4
  %740 = zext nneg i32 %739 to i64
  %741 = getelementptr inbounds nuw i8, ptr addrspace(1) %2, i64 %729
  %742 = getelementptr inbounds nuw i8, ptr addrspace(1) %741, i64 %738
  %743 = getelementptr inbounds nuw i8, ptr addrspace(1) %742, i64 %740
  store <4 x i32> %730, ptr addrspace(1) %743, align 1
  br label %744

744:                                              ; preds = %728, %402, %10, %7
  ret void
}
