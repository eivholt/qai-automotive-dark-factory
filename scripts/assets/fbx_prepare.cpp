// Local-only FBX preparation. Uses the FBX SDK installed with Unreal; ships no assets.
#include <fbxsdk.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace fs=std::filesystem;
struct Vec { float x=0,y=0,z=0; };
struct UV { float u=0,v=0; };
struct Tri { uint32_t v[3]; uint32_t island; Vec n[3]; UV uv[3]; };
struct Island { uint32_t id; size_t vertices=0, triangles=0; Vec lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f}; };
struct Mesh { std::string name,material; std::vector<Vec> points; std::vector<Tri> triangles; std::vector<Island> islands; };
struct Assignment { std::string group; Vec pivot; };
std::string quote(const std::string& s) { std::string r="\""; for(char c:s) { if(c=='"'||c=='\\') r+='\\'; if((unsigned char)c>=32) r+=c; } return r+'"'; }
std::string safe(std::string s) { for(char& c:s) if(!std::isalnum((unsigned char)c)&&c!='_') c='_'; return s; }
void expand(Island& a, Vec p) { a.lo.x=std::min(a.lo.x,p.x); a.lo.y=std::min(a.lo.y,p.y); a.lo.z=std::min(a.lo.z,p.z); a.hi.x=std::max(a.hi.x,p.x); a.hi.y=std::max(a.hi.y,p.y); a.hi.z=std::max(a.hi.z,p.z); }
std::string vec(Vec v) { std::ostringstream s; s<<std::setprecision(9)<<'['<<v.x<<','<<v.y<<','<<v.z<<']'; return s.str(); }
struct DSU { std::vector<uint32_t> p; DSU(size_t n):p(n){std::iota(p.begin(),p.end(),0);} uint32_t find(uint32_t x){while(p[x]!=x){p[x]=p[p[x]];x=p[x];}return x;} void join(uint32_t a,uint32_t b){a=find(a);b=find(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);} };
FbxAxisSystem axis() { return FbxAxisSystem(FbxAxisSystem::eZAxis,(FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd,FbxAxisSystem::eRightHanded); }
FbxScene* load(FbxManager* manager,const char* filename) {
 auto scene=FbxScene::Create(manager,"Source"); auto importer=FbxImporter::Create(manager,"");
 if(!importer->Initialize(filename,-1,manager->GetIOSettings()))throw std::runtime_error(importer->GetStatus().GetErrorString());
 if(!importer->Import(scene))throw std::runtime_error(importer->GetStatus().GetErrorString()); importer->Destroy();
 axis().ConvertScene(scene); FbxSystemUnit::cm.ConvertScene(scene);
 FbxGeometryConverter converter(manager); if(!converter.Triangulate(scene,true))throw std::runtime_error("Triangulation failed"); return scene;
}
void extract(FbxNode* node,std::vector<Mesh>& output) {
 if(auto mesh=node->GetMesh()) {
  Mesh out; out.name=safe(node->GetName()); out.material=node->GetMaterialCount()?node->GetMaterial(0)->GetName():"Default";
  FbxAMatrix geometric; geometric.SetT(node->GetGeometricTranslation(FbxNode::eSourcePivot)); geometric.SetR(node->GetGeometricRotation(FbxNode::eSourcePivot)); geometric.SetS(node->GetGeometricScaling(FbxNode::eSourcePivot));
  auto matrix=node->EvaluateGlobalTransform()*geometric; auto normalMatrix=matrix.Inverse().Transpose();
  out.points.resize(mesh->GetControlPointsCount());
  for(int i=0;i<mesh->GetControlPointsCount();++i){auto p=matrix.MultT(mesh->GetControlPointAt(i));out.points[i]={(float)p[0],(float)-p[1],(float)p[2]};}
  DSU dsu(out.points.size());
  FbxStringList uvNames; mesh->GetUVSetNames(uvNames);
  for(int p=0;p<mesh->GetPolygonCount();++p) {
   if(mesh->GetPolygonSize(p)!=3)throw std::runtime_error("Nontriangle after triangulation");
   Tri t{};
   for(int k=0;k<3;++k){
    t.v[k]=mesh->GetPolygonVertex(p,k); FbxVector4 n; mesh->GetPolygonVertexNormal(p,k,n);
    // Use the SDK multiplication convention for inverse-transpose normals.
    FbxVector4 nn=normalMatrix.MultT(n); // Same convention as Unreal FbxStaticMeshImport.
    double length=std::sqrt(nn[0]*nn[0]+nn[1]*nn[1]+nn[2]*nn[2]);
    if(length>1e-12)t.n[k]={(float)(nn[0]/length),(float)(-nn[1]/length),(float)(nn[2]/length)};
    FbxVector2 uv(0,0); bool unmapped=false;
    if(uvNames.GetCount())mesh->GetPolygonVertexUV(p,k,uvNames[0],uv,unmapped);
    t.uv[k]={(float)uv[0],(float)uv[1]};
   }
   dsu.join(t.v[0],t.v[1]);dsu.join(t.v[0],t.v[2]);
   // Reflection in original node transforms reverses surface orientation.
   if(matrix.Determinant()<0){std::swap(t.v[1],t.v[2]);std::swap(t.n[1],t.n[2]);std::swap(t.uv[1],t.uv[2]);}
   out.triangles.push_back(t);
  }
  std::map<uint32_t,uint32_t> islandIds;
  for(uint32_t i=0;i<out.points.size();++i){auto root=dsu.find(i);if(!islandIds.count(root)){uint32_t id=(uint32_t)islandIds.size();islandIds[root]=id;out.islands.push_back(Island{id});}}
  for(uint32_t i=0;i<out.points.size();++i){auto& island=out.islands[islandIds[dsu.find(i)]];++island.vertices;expand(island,out.points[i]);}
  for(auto& t:out.triangles){t.island=islandIds[dsu.find(t.v[0])];++out.islands[t.island].triangles;}
  std::cout<<out.name<<": "<<out.points.size()<<" vertices, "<<out.triangles.size()<<" triangles, "<<out.islands.size()<<" islands\n";
  output.push_back(std::move(out));
 }
 for(int i=0;i<node->GetChildCount();++i)extract(node->GetChild(i),output);
}
template<class T> void binary(std::ofstream& f,const T& value){f.write((const char*)&value,sizeof(value));}
void scan(const std::vector<Mesh>& meshes,const fs::path& dir){
 fs::create_directories(dir);std::ofstream j(dir/"islands.json");j<<"{\"version\":1,\"coordinates\":\"Unreal centimetres\",\"meshes\":[";
 bool firstMesh=true;
 for(const auto& m:meshes){
  if(!firstMesh)j<<',';firstMesh=false;
  j<<"{\"name\":"<<quote(m.name)<<",\"material\":"<<quote(m.material)<<",\"vertices\":"<<m.points.size()<<",\"triangles\":"<<m.triangles.size()<<",\"islands\":[";
  bool first=true;for(const auto& a:m.islands){if(!first)j<<',';first=false;j<<"{\"id\":"<<a.id<<",\"vertices\":"<<a.vertices<<",\"triangles\":"<<a.triangles<<",\"min\":"<<vec(a.lo)<<",\"max\":"<<vec(a.hi)<<'}';}j<<"]}";
  std::ofstream f(dir/(m.name+".meshbin"),std::ios::binary);f.write("DFMESH01",8);uint32_t nv=(uint32_t)m.points.size(),nt=(uint32_t)m.triangles.size();binary(f,nv);binary(f,nt);
  f.write((const char*)m.points.data(),m.points.size()*sizeof(Vec));
  for(const auto& t:m.triangles){for(auto idx:t.v)binary(f,idx);binary(f,t.island);for(auto n:t.n)binary(f,n);for(auto uv:t.uv)binary(f,uv);}
 }
 j<<"]}\n";
}
void outputGroups(FbxManager* manager,const std::vector<Mesh>& meshes,const char* assignments,const fs::path& filename,const std::set<std::string>& selectedGroups={}){
 std::map<std::pair<std::string,int>,Assignment> map;std::ifstream input(assignments);if(!input)throw std::runtime_error("Cannot open assignment TSV");
 std::string line;
 while(std::getline(input,line)){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string mesh;int island;Assignment a;row>>mesh>>island>>a.group>>a.pivot.x>>a.pivot.y>>a.pivot.z;if(!row)throw std::runtime_error("Bad assignment row");if(!std::isfinite(a.pivot.x)||!std::isfinite(a.pivot.y)||!std::isfinite(a.pivot.z))throw std::runtime_error("Non-finite assignment pivot"); if(!map.emplace(std::make_pair(mesh,island),a).second)throw std::runtime_error("Duplicate island assignment");}
 auto scene=FbxScene::Create(manager,"WeldingCellPrepared");scene->GetGlobalSettings().SetAxisSystem(axis());scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::cm);
 struct Group{FbxMesh* mesh=nullptr;FbxNode* node=nullptr;std::vector<FbxVector4> points;std::map<std::pair<int,uint32_t>,int> pointMap;std::map<std::string,int> materials;Vec pivot;FbxGeometryElementNormal* normals;FbxGeometryElementUV* uvs;FbxGeometryElementMaterial* material;};
 std::map<std::string,Group> groups;std::map<std::string,FbxSurfacePhong*> materials;
 size_t inputTriangles=0,selectedInputTriangles=0,outputTriangles=0;
 for(int mi=0;mi<(int)meshes.size();++mi){const auto& m=meshes[mi];
  for(const auto& t:m.triangles){++inputTriangles;auto it=map.find({m.name,(int)t.island});if(it==map.end())throw std::runtime_error("Missing island assignment: "+m.name+" "+std::to_string(t.island));const auto& a=it->second;
   if(!selectedGroups.empty()&&!selectedGroups.count(a.group))continue;
   ++selectedInputTriangles;
   auto& g=groups[a.group];
   if(!g.mesh){g.pivot=a.pivot;g.node=FbxNode::Create(scene,a.group.c_str());g.mesh=FbxMesh::Create(scene,a.group.c_str());g.node->SetNodeAttribute(g.mesh);scene->GetRootNode()->AddChild(g.node);g.normals=g.mesh->CreateElementNormal();g.normals->SetMappingMode(FbxLayerElement::eByPolygonVertex);g.normals->SetReferenceMode(FbxLayerElement::eDirect);g.uvs=g.mesh->CreateElementUV("UVChannel_1");g.uvs->SetMappingMode(FbxLayerElement::eByPolygonVertex);g.uvs->SetReferenceMode(FbxLayerElement::eDirect);g.material=g.mesh->CreateElementMaterial();g.material->SetMappingMode(FbxLayerElement::eByPolygon);g.material->SetReferenceMode(FbxLayerElement::eIndexToDirect);}
   if(std::abs(g.pivot.x-a.pivot.x)>0.0001f||std::abs(g.pivot.y-a.pivot.y)>0.0001f||std::abs(g.pivot.z-a.pivot.z)>0.0001f)throw std::runtime_error("Inconsistent group pivots");
   if(!g.materials.count(m.material)){if(!materials.count(m.material)){auto mat=FbxSurfacePhong::Create(scene,m.material.c_str());mat->Diffuse.Set(FbxDouble3(.5,.5,.5));materials[m.material]=mat;}g.materials[m.material]=g.node->AddMaterial(materials[m.material]);}
   int polygon[3];
   for(int k=0;k<3;++k){auto key=std::make_pair(mi,t.v[k]);auto vi=g.pointMap.find(key);if(vi==g.pointMap.end()){int id=(int)g.points.size();Vec p=m.points[t.v[k]];g.points.emplace_back(p.x-a.pivot.x,-(p.y-a.pivot.y),p.z-a.pivot.z);g.pointMap[key]=id;polygon[k]=id;}else polygon[k]=vi->second;}
   g.mesh->BeginPolygon(g.materials[m.material]);for(int k=0;k<3;++k){g.mesh->AddPolygon(polygon[k]);auto n=t.n[k];g.normals->GetDirectArray().Add(FbxVector4(n.x,-n.y,n.z));auto uv=t.uv[k];g.uvs->GetDirectArray().Add(FbxVector2(uv.u,uv.v));}g.mesh->EndPolygon();++outputTriangles;
  }
 }
 for(const auto& name:selectedGroups)if(!groups.count(name))throw std::runtime_error("Selected group is absent or empty: "+name);
 if(outputTriangles!=selectedInputTriangles||groups.empty())throw std::runtime_error("Selected triangle conservation failed");
 for(auto& pair:groups){auto& g=pair.second;g.mesh->InitControlPoints((int)g.points.size());std::copy(g.points.begin(),g.points.end(),g.mesh->GetControlPoints());}
 fs::create_directories(filename.parent_path());auto exporter=FbxExporter::Create(manager,"");
 if(!exporter->Initialize(filename.string().c_str(),-1,manager->GetIOSettings()))throw std::runtime_error(exporter->GetStatus().GetErrorString());
 if(!exporter->Export(scene))throw std::runtime_error(exporter->GetStatus().GetErrorString());exporter->Destroy();
 std::cout<<"Exported "<<groups.size()<<" grouped meshes, "<<outputTriangles<<" / "<<inputTriangles<<" triangles to "<<filename.string()<<"\n";
 if(selectedGroups.empty()&&inputTriangles!=outputTriangles)throw std::runtime_error("Triangle conservation failed");
}
int main(int argc,char** argv){try{
 if(argc<4){std::cerr<<"Usage: fbx_prepare scan SOURCE.fbx OUTDIR | group SOURCE.fbx ASSIGNMENTS.tsv OUTPUT.fbx | group-subset SOURCE.fbx ASSIGNMENTS.tsv OUTPUT.fbx GROUPS.txt\n";return 2;}
 if(((std::string(argv[1])=="group"&&argc==5)||(std::string(argv[1])=="group-subset"&&argc==6)) && fs::weakly_canonical(argv[2])==fs::weakly_canonical(argv[4]))throw std::runtime_error("Refusing to overwrite source FBX");
 auto manager=FbxManager::Create();manager->SetIOSettings(FbxIOSettings::Create(manager,IOSROOT));auto scene=load(manager,argv[2]);std::vector<Mesh> meshes;extract(scene->GetRootNode(),meshes);
 if(std::string(argv[1])=="scan")scan(meshes,argv[3]);else if(std::string(argv[1])=="group"&&argc==5)outputGroups(manager,meshes,argv[3],argv[4]);else if(std::string(argv[1])=="group-subset"&&argc==6){
  std::ifstream list(argv[5]);if(!list)throw std::runtime_error("Cannot open group selection file");
  std::set<std::string> selected;std::string line;
  while(std::getline(list,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;if(safe(line)!=line||!selected.insert(line).second)throw std::runtime_error("Invalid or duplicate selected group: "+line);}
  if(selected.empty())throw std::runtime_error("Empty group selection");
  outputGroups(manager,meshes,argv[3],argv[4],selected);
 }else throw std::runtime_error("Unknown mode");
 manager->Destroy();return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}