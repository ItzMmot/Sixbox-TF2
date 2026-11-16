#include "../SixBox/src/Features/Misc/AutoDodge/DodgePolicy.h"
#include <iostream>

// CPU-only geometry workload. Native movement/trace costs must be checked in
// game; this benchmark deliberately makes no assertions about client FPS.
int main()
{
	std::vector<Dodge::Body> path(54,{{0,0,0},{-24,-24,0},{24,24,82}});
	std::vector<Dodge::Threat> threats(32);
	for(int n=0;n<32;++n)
		for(int tick=0;tick<53;++tick)
		{
			Dodge::Step step;
			step.from={float(n*10-160),float(tick*4-106),20};
			step.to=step.from+Dodge::V{5,1,0};
			step.hull={2,2,2};step.radius=146;step.area=true;
			threats[n].steps.push_back(step);
		}
	volatile float checksum=0;
	const auto start=std::chrono::steady_clock::now();
	for(int iteration=0;iteration<500;++iteration)
	{
		auto score=Dodge::Evaluate(path,threats,48,.015f,[](size_t,size_t,Dodge::V,Dodge::V,Dodge::V){return 1;});
		if(!score.complete) return 1;
		checksum=checksum+score.damage;
	}
	const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
	std::cout<<"500 geometry evaluations: "<<elapsed<<" ms; checksum: "<<checksum<<'\n';
}
