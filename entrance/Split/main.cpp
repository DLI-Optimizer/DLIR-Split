#include "ModelAnalyze/ModelAnalyzer.h"
#include "cmdline.h"
using namespace std;

int main(int argc, char *argv[])
{
    cmdline::parser parser;
    parser.set_program_name("DLIR-SPLIT");
    parser.add<int>("count", 'c', "how many count to split [>0]", false, 3);
    parser.add<std::string>("model", 'm', "model name", false, "vgg19");

    parser.parse_check(argc, argv);
    int count = parser.get<int>("count") > 0 ? parser.get<int>("count") : 3;
    std::string model_name=parser.get<std::string>("model");

    cout<<"=> split "<<model_name<<" to "<<count<<endl;
    ModelAnalyzer analyzer = ModelAnalyzer(model_name);
    if(!analyzer.UniformSplit(count,"RTX-2080Ti",true,true,100,50))
    {
        cout<<"bad aim"<<endl;
    }
    else
    {
        cout<<"run to end."<<endl;
    }

    return 0;
}