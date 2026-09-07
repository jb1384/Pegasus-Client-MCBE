#include "Navigation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace utility::navigation {
namespace {
bool integer(std::string_view s,int& out) {
    if(s.empty())return false;
    if(s.front()=='+'){s.remove_prefix(1);if(s.empty()||s.front()=='-'||s.front()=='+')return false;}
    const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),out);
    return error==std::errc{} && end==s.data()+s.size();
}
bool coordinate(std::string_view s,Coordinate& out) {
    out.relative=s.starts_with('~');if(out.relative)s.remove_prefix(1);
    if(out.relative&&s.empty()){out.value=0;return true;}
    return integer(s,out.value);
}
std::string lowercase(std::string value) {
    for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;
}
bool identifier(std::string_view value) {
    const auto colon=value.find(':');
    if(colon==0||colon==value.size()-1)return false;
    int colons{};
    for(char c:value) {
        if(c==':'){if(++colons>1)return false;continue;}
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'||c=='/'))return false;
    }
    return !value.empty();
}
}
ParseResult parse_command(const std::vector<std::string>& args) {
    ParseResult r;if(args.empty())return r;
    const auto name=lowercase(args[0]);Command command;
    if(name=="goto") {
        r.handled=true;command.kind=CommandKind::go;
        const auto bad=[&]{r.error="Usage: ,goto <x> <z> or ,goto <x> <y> <z> (integers or ~offset).";};
        if(args.size()!=3&&args.size()!=4){bad();return r;}
        command.has_y=args.size()==4;
        if(!coordinate(args[1],command.x)||!coordinate(args.back(),command.z)||
            (command.has_y&&!coordinate(args[2],command.y))){bad();return r;}
    } else if(name=="mine") {
        r.handled=true;command.kind=CommandKind::mine;
        if(args.size()<2){r.error="Usage: ,mine [quantity] <block> [block...]";return r;}
        std::size_t first=1;
        if(!args[1].empty() && ((args[1][0]>='0'&&args[1][0]<='9')||args[1][0]=='-'||args[1][0]=='+')) {
            if(!integer(args[1],command.quantity)||command.quantity<=0){r.error="Mining quantity must be a positive integer.";return r;}
            ++first;
        }
        if(first==args.size()){r.error="Usage: ,mine [quantity] <block> [block...]";return r;}
        for(auto i=first;i<args.size();++i) {
            auto block=lowercase(args[i]);
            if(!identifier(block)){r.error="Invalid block identifier: "+args[i];return r;}
            if(block.find(':')==block.npos)block="minecraft:"+block;
            if(std::find(command.blocks.begin(),command.blocks.end(),block)==command.blocks.end())command.blocks.push_back(std::move(block));
        }
        if(command.blocks.size()>64){r.error="Choose at most 64 block identifiers.";return r;}
    } else if(name=="pause"||name=="resume"||name=="stop") {
        r.handled=true;
        if(args.size()!=1){r.error="Usage: ,"+name+" (no arguments).";return r;}
        command.kind=name=="pause"?CommandKind::pause:name=="resume"?CommandKind::resume:CommandKind::stop;
    } else if(name=="baritone") {
        r.handled=true;
        if(args.size()!=2||lowercase(args[1])!="status"){r.error="Usage: ,baritone status";return r;}
        command.kind=CommandKind::status;
    } else return r;
    r.command=std::move(command);return r;
}
std::optional<BlockPos> resolve(const Command& c,Vec3 feet) {
    if(!std::isfinite(feet.x)||!std::isfinite(feet.y)||!std::isfinite(feet.z))return {};
    const auto value=[](Coordinate coord,double origin)->std::optional<int> {
        const double result=coord.value+(coord.relative?std::floor(origin):0.0);
        if(result < -30000000 || result > 30000000)return {};
        return static_cast<int>(result);
    };
    const auto x=value(c.x,feet.x),z=value(c.z,feet.z),y=c.has_y?value(c.y,feet.y):std::optional<int>(0);
    if(!x||!y||!z)return {};
    return BlockPos{*x,*y,*z};
}
} // namespace utility::navigation
