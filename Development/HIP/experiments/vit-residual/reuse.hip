// Forced residual reuse probe; no adaptive correctness or scene-change policy is implied.
WAVE void vit_residual_apply(const float*current,const float*anchor_in,const float*anchor_out,float*out,uint count){
 uint at=bid()*256+__builtin_amdgcn_workitem_id_x();
 for(uint j=0;j<8;j++,at+=32)if(at<count){float x=current[at],a=anchor_in[at],b=anchor_out[at];out[at]=x==a?b:F(H(x+(b-a)));}
}
