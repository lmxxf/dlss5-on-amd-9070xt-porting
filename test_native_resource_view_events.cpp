#include "native_resource_view_events.cpp"
int main(){
 reshade::api::resource_view views[2]={{0x1234},{0x5678}};
 reshade::api::descriptor_table_update update{};
 update.table={0xf000000220000064ull};update.binding=0;update.array_offset=0;
 update.count=2;update.type=reshade::api::descriptor_type::texture_unordered_access_view;update.descriptors=views;
 if(table_update(nullptr,1,&update)||table_update(nullptr,0,nullptr))return 1;
 if(update.table.handle!=0xf000000220000064ull||update.binding||update.array_offset||update.count!=2||update.descriptors!=views||views[0].handle!=0x1234||views[1].handle!=0x5678)return 2;
 reshade::api::descriptor_table_copy copy{};copy.source_table=update.table;copy.dest_table={0xf000000220000084ull};copy.count=2;
 if(table_copy(nullptr,1,&copy)||table_copy(nullptr,0,nullptr)||copy.source_table.handle!=update.table.handle||copy.dest_table.handle!=0xf000000220000084ull||copy.count!=2)return 3;
 tracked_resources.insert(0x99);reshade::api::resource source{0x99},dest{0x88};reshade::api::subresource_box box{0,0,0,16,16,1};
 if(resource_copy(nullptr,source,dest)||texture_copy(nullptr,source,0,&box,dest,0,&box,reshade::api::filter_mode::min_mag_mip_point))return 4;
 if(resource_copy_count!=2||box.right!=16||box.bottom!=16||source.handle!=0x99||dest.handle!=0x88)return 5;
 resource_destroy(nullptr,source);if(tracked_resources.count(source.handle))return 6;
 if(resource_copy(nullptr,source,dest)||resource_copy_count!=2)return 7;
 puts("event observers: no suppression or payload mutation, resource lifetime filtering pass; no live ABI acceptance");return 0;
}
