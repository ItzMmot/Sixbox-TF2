#pragma once
#include "JumpSolver.h"
#include <array>
#include <vector>

namespace StickyJump
{
    enum class Phase { Idle, Search, Validate, Approach, Launch, Verify };
    struct Frame
    {
        int command = 0;
        uint32_t weapon = 0;
        double wallTime = 0;
        float gameTime = 0, projectileTime = 0, interval = .015f, yaw = 0, pitch = 0;
        Body body;
        bool enabled = false, pressed = false, eligible = false, grounded = false;
        bool manualCombat = false, dodge = false, shifting = false, canDetonate = false;
        std::vector<Bomb> bombs;
    };
    inline bool LaunchArmed(const Frame& frame)
    {
        // Projectile creation is stamped in the client's render clock, not the
        // player's predicted tickbase. Leave two server ticks at the boundary.
        const float time=frame.projectileTime-2.f*frame.interval;
        return !frame.bombs.empty() && std::all_of(frame.bombs.begin(),frame.bombs.end(),
            [&](const Bomb& bomb){return Armed(bomb,time);});
    }
    struct Output
    {
        bool controls = false, detonate = false, duck = false, jump = false;
    };
    struct Display
    {
        bool active = false, markers = false, automatic = false;
        Direction direction = Direction::Diagonal;
        const char* status = "Idle";
        float target = 0, predicted = 0, measured = 0, power = 0, elevation = 0;
        double expires = 0;
        Candidate plan;
    };
    enum class SampleResult { Pending, Sample, Done };

    // World supplies incremental candidate generation, exact movement probes,
    // live blast validation and a monotonic clock. Tests use the same controller.
    class Controller
    {
        Phase m_phase = Phase::Idle;
        Request m_request;
        std::array<Search,3> m_search;
        Candidate m_plan;
        Display m_display;
        int m_lastCommand = 0, m_cursor = 0, m_launchTick = 0, m_jumpProbe = -1;
        uint32_t m_weapon = 0;
        double m_started = 0, m_stageTime = 0;
        float m_target = 0;
        std::vector<uint32_t> m_beforeDetonation;
        std::vector<Bomb> m_observed;
        Body m_beforeBlast;
        Output m_output;
        bool m_observeLaunch=false;

        static std::vector<uint32_t> Ids(const std::vector<Bomb>& bombs)
        {
            std::vector<uint32_t> result;
            for (const auto& b:bombs) result.push_back(b.id);
            std::sort(result.begin(),result.end());
            return result;
        }
        void Stage(Phase phase, const Frame& f, const char* status)
        {
            m_phase=phase; m_stageTime=f.wallTime; m_display.status=status; m_jumpProbe=-1; m_observeLaunch=false;
        }
        void Finish(const Frame& f, const char* reason)
        {
            m_phase=Phase::Idle; m_output={}; m_display.active=false;
            m_display.markers=false; m_display.status=reason; m_display.expires=f.wallTime+3;
        }
        bool Changed(const Frame& f) const
        {
            if (Ids(f.bombs)!=Ids(m_observed)) return true;
            for (size_t i=0;i<f.bombs.size();++i)
            {
                const auto& a=f.bombs[i];
                const auto it=std::find_if(m_observed.begin(),m_observed.end(),[&](const Bomb& b){return b.id==a.id;});
                if (it==m_observed.end() || a.touched!=it->touched
                    || (a.touched && (a.position-it->position).Length()>4.f)) return true;
            }
            return false;
        }
    public:
        void Reset() { *this=Controller{}; }
        bool Busy() const { return m_phase!=Phase::Idle; }
        Phase State() const { return m_phase; }
        const Display& Snapshot() const { return m_display; }
        const Output& Current() const { return m_output; }

        template<class World> Output Run(const Frame& f, Request settings, World& world)
        {
            // Replayed commands reuse intent and never advance a sequence twice.
            if (f.command<=0) return {};
            if (f.command==m_lastCommand) return m_output;
            if (f.command<m_lastCommand) return {};
            m_lastCommand=f.command;
            m_output={};
            const double deadline=world.Now()+.001;
            if (Busy() && (!f.enabled || !f.eligible || f.weapon!=m_weapon || f.dodge
                || f.shifting || f.manualCombat))
            {
                Finish(f,f.dodge ? "Cancelled: Auto Dodge" : "Cancelled: player input or state changed");
                return {};
            }
            if (Busy() && f.pressed) { Finish(f,"Cancelled"); return {}; }
            if (Busy() && f.wallTime-m_started>=8) { Finish(f,"No reachable launch within 8 seconds"); return {}; }
            if (!Busy())
            {
                if (!f.enabled || !f.pressed) return {};
                if (!f.eligible || f.dodge || f.shifting) { Finish(f,"Unsupported weapon or player state"); return {}; }
                if (!(settings.power>0) || !std::isfinite(settings.power)) { Finish(f,"Power is zero"); return {}; }
                if (f.manualCombat) { Finish(f,"Release fire/jump to start"); return {}; }
                if (f.bombs.empty())
                { Finish(f,"No owned stickies for this launcher"); return {}; }
                m_request=settings; m_request.power=std::clamp(settings.power,0.f,100.f);
                m_request.yaw=f.yaw; m_request.start=f.body.origin;
                m_started=f.wallTime; m_weapon=f.weapon; m_launchTick=0; m_target=0;
                m_search={}; m_cursor=0; m_observed=f.bombs;
                m_display={}; m_display.active=true; m_display.power=m_request.power;
                m_display.automatic=m_request.automatic;
                world.BeginSearch(f,m_request);
                Stage(Phase::Search,f,"Calculating launch");
            }
            m_output.controls=true;
            m_display.expires=f.wallTime+1;

            if (m_phase==Phase::Verify)
            {
                bool consumed=false;
                const auto currentIds=Ids(f.bombs);
                for (auto id:m_beforeDetonation)
                    if (!std::binary_search(currentIds.begin(),currentIds.end(),id)) { consumed=true; break; }
                // Confirmation is based on observed entities/velocity, never only
                // on the fact that an attack button was written to a command.
                const float measured=world.MeasuredBoost(m_beforeBlast,f);
                if (consumed && !f.grounded && measured>20.f)
                {
                    m_display.measured=measured;
                    Finish(f,"Launch completed");
                }
                else if (f.wallTime-m_stageTime>.75) Finish(f,"Detonation was not confirmed");
                return m_output;
            }
            if (m_request.automatic && m_phase!=Phase::Launch)
            {
                // Source pitch is positive down; the presets use elevation above the horizon.
                m_display.elevation=-std::remainder(f.pitch,360.f);
                m_request.direction=DirectionForElevation(m_display.elevation);
                m_display.direction=m_request.direction;
            }
            if (Changed(f))
            {
                m_observed=f.bombs;
                if (m_phase!=Phase::Search)
                {
                    if (f.bombs.empty()) { Finish(f,"All assist stickies disappeared"); return m_output; }
                    if (m_phase!=Phase::Launch) Stage(Phase::Approach,f,"Updating assist stickies");
                }
                else
                {
                    m_cursor=0; m_search={};
                    world.BeginSearch(f,m_request);
                    Stage(Phase::Search,f,"Updating changed stickies");
                }
            }
            if (m_phase==Phase::Search)
            {
                while (world.Now()<deadline)
                {
                    Candidate candidate;
                    const auto result=world.NextCandidate(m_cursor,f,m_request,candidate);
                    if (result==SampleResult::Pending) continue;
                    if (result==SampleResult::Sample)
                    {
                        ++m_cursor;
                        // Auto shares the same geometry samples across pitches.
                        if (m_request.automatic)
                        {
                            for (auto direction:{Direction::Horizontal,Direction::Diagonal,Direction::Vertical})
                                m_search[int(direction)].Add(candidate,DesiredDirection(direction,m_request.yaw));
                        }
                        else m_search[int(m_request.direction)].Add(candidate,m_request.Wish());
                        continue;
                    }
                    if (std::none_of(m_search.begin(),m_search.end(),[](const Search& search){return search.Maximum()>0;}))
                    { Finish(f,"No reachable launch in this direction"); return m_output; }
                    Stage(Phase::Validate,f,"Checking movement and clearance");
                    break;
                }
            }
            if (m_phase==Phase::Validate || m_phase==Phase::Approach)
            {
                m_target=m_search[int(m_request.direction)].Maximum()*m_request.power*.01f;
                m_display.target=m_target;
            }
            if (m_phase==Phase::Validate && world.Now()<deadline)
            {
                m_plan={}; m_plan.jumpTicks=2;
                Stage(Phase::Approach,f,"Waiting for assist launch window");
            }
            if (m_phase==Phase::Approach || m_phase==Phase::Launch)
            {
                if (!(m_target>0))
                {
                    m_display.status="No reachable launch at this pitch";
                    m_display.predicted=0;
                    return m_output;
                }
                // Holding crouch is sequence state, not optional planning work.
                // An expensive live probe must never release it mid-flight.
                if (m_phase==Phase::Launch)
                {
                    m_output.duck=true;
                    if (++m_launchTick>20) { Finish(f,"Launch window was missed"); return m_output; }
                }
                if (world.Now()>=deadline) return m_output;
                if (!LaunchArmed(f))
                {
                    m_display.status="Waiting for sticky arming";
                    return m_output;
                }
                if (!f.grounded) m_jumpProbe=-1;
                if (m_phase==Phase::Approach && m_jumpProbe>=0
                    && !m_observeLaunch)
                {
                    if (!f.canDetonate) { m_display.status="Waiting for detonation cooldown"; return m_output; }
                    // Give jump validation its own command's budget. Running a
                    // Ready probe first every time can starve this operation.
                    Candidate live=m_plan;
                    // A one-tick window needs jump + detonation on the same
                    // command and is handled by Ready, before this phase.
                    live.jumpTicks=m_jumpProbe+2;
                    if (world.CanBeginJump(f,m_request,live,m_target))
                    {
                        m_plan.jumpTicks=live.jumpTicks;
                        m_plan.takeoff=f.body.origin; m_plan.delta=live.delta;
                        m_display.plan=m_plan; m_display.markers=true; m_display.predicted=live.delta.Length();
                        m_output.duck=true; m_output.jump=f.grounded; m_launchTick=1;
                        Stage(Phase::Launch,f,"Waiting for launch window");
                    }
                    else if (++m_jumpProbe>=15)
                    {
                        Stage(Phase::Approach,f,"Waiting for assist launch window");
                    }
                    else
                    {
                        m_display.status="Checking live crouch-jump timing";
                        m_observeLaunch=true;
                    }
                    return m_output;
                }
                m_observeLaunch=false;
                Vec delta;
                // The adapter tests this command's actual movement and current
                // bombs again; an old candidate is never permission to detonate.
                const bool ready=world.Ready(f,m_request,m_target,delta);
                m_display.predicted=delta.Length();
                if (ready && f.canDetonate)
                {
                    m_output.duck=true; m_output.jump=f.grounded; m_output.detonate=true;
                    m_plan.takeoff=f.body.origin; m_plan.delta=delta;
                    m_display.plan=m_plan; m_display.markers=true;
                    m_beforeDetonation=Ids(f.bombs); m_beforeBlast=f.body;
                    world.RecordBaseline(f);
                    Stage(Phase::Verify,f,"Confirming launch");
                }
                else if (m_phase==Phase::Approach && f.canDetonate && f.grounded)
                {
                    if (m_jumpProbe<0) m_jumpProbe=0;
                    m_display.status="Checking live crouch-jump timing";
                }
                else m_display.status=m_phase==Phase::Launch ? "Waiting for launch window"
                    : !f.canDetonate ? "Waiting for detonation cooldown"
                    : "Waiting for assist launch window";
            }
            return m_output;
        }
    };
}
