// Original: volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/util/hkpworldagentutil.cpp
//
// Havok 2014.2.5 (havok_impl.md HK7): agents between world entities (created on new broadphase pairs, in the
// island of the non-fixed entity, with the contact manager of the two materials' response types), from rfg.exe.

#include <stdio.h>
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/util/hkpworldagentutil.h"

#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/agent/collidable/hkpcollidable.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/agent/contactmgr/hkpcontactmgr.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/agent/hkpprocesscollisioninput.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/agent3/machine/nn/hkpagentnnmachine.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/agent3/machine/nn/hkplinkedcollidable.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/dispatch/contactmgr/hkpcontactmgrfactory.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/dispatch/hkpcollisiondispatcher.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/collide/shape/hkpshape.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/entity/hkpentity.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/hkpsimulationisland.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/hkpworld.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/simulation/hkpsimulation.h"
#include "volition/libmodules/havok/hk2014_2_5_r1/source/physics2012/dynamics/world/util/hkpworldoperationutil.h"

static inline hkpEntity* hkWorldAgentUtil_entity(const hkpCollidable* collidable)
{
    return static_cast<hkpEntity*>((hkpWorldObject*)collidable->getOwner());
}

static inline hkpAgentNnTrack& hkWorldAgentUtil_track(hkpSimulationIsland* island, unsigned char nnTrackType)
{
    return nnTrackType == HK_AGENT3_NARROWPHASE_TRACK ? island->m_narrowphaseAgentTrack : island->m_midphaseAgentTrack;
}

// 0x00E1DDB0: the island whose track holds the entry (only the track with fewer sectors is searched)
hkpSimulationIsland* hkpWorldAgentUtil::getIslandFromAgentEntry(hkpAgentNnEntry* entry, hkpSimulationIsland* candidateA, hkpSimulationIsland* candidateB)
{
    hkpAgentNnTrack& trackA = hkWorldAgentUtil_track(candidateA, entry->m_nnTrackType.m_storage);
    hkpAgentNnTrack& trackB = hkWorldAgentUtil_track(candidateB, entry->m_nnTrackType.m_storage);
    const bool searchA = trackA.m_sectors.getSize() <= trackB.m_sectors.getSize();
    hkpAgentNnTrack& track = searchA ? trackA : trackB;
    // hkAgentNnMachine_IsEntryOnTrack (inlined)
    bool found = false;
    for (int i = 0; i < track.m_sectors.getSize(); i++)
    {
        hkpAgentNnSector* sector = track.m_sectors[i];
        if (sector->getBegin() <= entry && entry < sector->getEnd())
        {
            found = true;
            break;
        }
    }
    return found == searchA ? candidateA : candidateB;
}

// 0x00E1DE40
void hkpWorldAgentUtil::invalidateTim(hkpEntity* entity, const hkpCollisionInput& collisionInput)
{
    hkArray<hkpLinkedCollidable::CollisionEntry>& entries = entity->m_collidable.m_collisionEntries;
    for (int i = 0; i < entries.getSize(); i++)
    {
        hkAgentNnMachine_InvalidateTimInAgent(entries[i].m_agentEntry, collisionInput);
    }
}

// 0x00E1DE70: every agent of the island, narrowphase track first
void hkpWorldAgentUtil::warpTime(hkpSimulationIsland* island, float oldTime, float newTime, const hkpCollisionInput& collisionInput)
{
    HK_FOR_ALL_AGENT_ENTRIES_BEGIN(island->m_narrowphaseAgentTrack, entry)
    {
        hkAgentNnMachine_WarpTimeInAgent(entry, oldTime, newTime, collisionInput);
    }
    HK_FOR_ALL_AGENT_ENTRIES_END;
    HK_FOR_ALL_AGENT_ENTRIES_BEGIN(island->m_midphaseAgentTrack, entry)
    {
        hkAgentNnMachine_WarpTimeInAgent(entry, oldTime, newTime, collisionInput);
    }
    HK_FOR_ALL_AGENT_ENTRIES_END;
}

// 0x00E1DF40: the islands of two non-fixed entities merge first; the agent goes to the first island that is not the
// fixed one (storage index 0xFFFF), on its narrowphase track
hkpAgentNnEntry* hkpWorldAgentUtil::addAgent(hkpLinkedCollidable* collA, hkpLinkedCollidable* collB, const hkpProcessCollisionInput& input)
{
    hkpEntity* entityA = hkWorldAgentUtil_entity(collA);
    hkpEntity* entityB = hkWorldAgentUtil_entity(collB);
    if (entityA->m_motion.m_type.m_storage != hkpMotion::MOTION_FIXED && entityB->m_motion.m_type.m_storage != hkpMotion::MOTION_FIXED && entityA->m_simulationIsland != entityB->m_simulationIsland)
    {
        hkpWorldOperationUtil::mergeIslands(entityA->m_world, entityA, entityB);
    }
    hkpSimulationIsland* islandA = entityA->m_simulationIsland;
    hkpSimulationIsland* islandB = entityB->m_simulationIsland;
    hkpSimulationIsland* theIsland = islandA;
    if (islandA->m_storageIndex == 0xFFFF && islandB->m_storageIndex != 0xFFFF)
    {
        theIsland = islandB;
    }

    // transform shapes are unwrapped (up to 4 levels each)
    hkPadSpu<unsigned char> cdBodyHasTransformFlag(0);
    hkpCdBody newCdBodiesA[4];
    hkMotionState newMotionStatesA[4];
    hkpCdBody newCdBodiesB[4];
    hkMotionState newMotionStatesB[4];
    const hkpCdBody* firstNonTransformBodyA = collA;
    const hkpCdBody* firstNonTransformBodyB = collB;
    if (collA->m_shape->m_type.m_storage == hkcdShapeType::TRANSFORM)
    {
        firstNonTransformBodyA = hkAgentMachine_processTransformedShapes(collA, newCdBodiesA, newMotionStatesA, 4, cdBodyHasTransformFlag);
    }
    if (collB->m_shape->m_type.m_storage == hkcdShapeType::TRANSFORM)
    {
        firstNonTransformBodyB = hkAgentMachine_processTransformedShapes(collB, newCdBodiesB, newMotionStatesB, 4, cdBodyHasTransformFlag);
    }

    int agentType;
    int isFlipped;
    hkAgentNnMachine_GetAgentType(firstNonTransformBodyA, firstNonTransformBodyB, input, agentType, isFlipped);
    if (isFlipped)
    {
        hkpLinkedCollidable* coll = collA;
        collA = collB;
        collB = coll;
        const hkpCdBody* body = firstNonTransformBodyA;
        firstNonTransformBodyA = firstNonTransformBodyB;
        firstNonTransformBodyB = body;
    }

    // the factory is picked by the unflipped entities' response types
    hkpContactMgrFactory* factory = input.m_dispatcher.val()->m_contactMgrFactory[entityA->m_material.m_responseType.m_storage][entityB->m_material.m_responseType.m_storage];
    hkpContactMgr* mgr = factory->createContactMgr(*collA, *collB, input);
    return hkAgentNnMachine_CreateAgent(theIsland->m_narrowphaseAgentTrack, collA, firstNonTransformBodyA, collB, firstNonTransformBodyB, cdBodyHasTransformFlag.val(), agentType, input, mgr);
}

// 0x00E1E0B0: both islands need a split check (one only when the islands are the same or one entity is fixed)
void hkpWorldAgentUtil::removeAgent(hkpAgentNnEntry* agent)
{
    hkpEntity* entityA = hkWorldAgentUtil_entity(agent->m_collidable[0]);
    hkpEntity* entityB = hkWorldAgentUtil_entity(agent->m_collidable[1]);
    hkpSimulationIsland* islandA = entityA->m_simulationIsland;
    hkpSimulationIsland* islandB = entityB->m_simulationIsland;
    hkpSimulationIsland* theIsland;
    if (islandA == islandB)
    {
        islandA->m_splitCheckRequested = true;
        theIsland = islandA;
    }
    else if (entityA->m_motion.m_type.m_storage == hkpMotion::MOTION_FIXED)
    {
        theIsland = islandB;
    }
    else if (entityB->m_motion.m_type.m_storage == hkpMotion::MOTION_FIXED)
    {
        theIsland = islandA;
    }
    else
    {
        theIsland = getIslandFromAgentEntry(agent, islandA, islandB);
        islandA->m_splitCheckRequested = true;
        entityB->m_simulationIsland->m_splitCheckRequested = true;
    }
#if 0
    {
        extern void openrfg_log(const char*); char b[400];
        snprintf(b,sizeof b,"DBGRM agent=%p cA=%p cB=%p eA=%p eB=%p iA=%p iB=%p isl=%p world=%p disp=%p cmd=%d type=%d track=%d size=%d",(void*)agent,(void*)agent->m_collidable[0],(void*)agent->m_collidable[1],(void*)entityA,(void*)entityB,(void*)islandA,(void*)islandB,(void*)theIsland,(void*)0,(void*)0,(int)agent->m_streamCommand,(int)agent->m_agentType,(int)agent->m_nnTrackType.m_storage,(int)agent->m_size);
        openrfg_log(b);
        { size_t off=(size_t)((char*)&entityA->m_simulationIsland-(char*)entityA); size_t offB=(size_t)((char*)&entityB->m_simulationIsland-(char*)entityB);
          snprintf(b,sizeof b,"DBGRM2 islOff=%zu sizeofEntity=%zu offColl=%zu offMot=%zu",off,sizeof(hkpEntity),(size_t)((char*)&entityA->m_collidable-(char*)entityA),(size_t)((char*)&entityA->m_motion-(char*)entityA)); openrfg_log(b);
          const unsigned* w=(const unsigned*)entityA; char*q=b; q+=snprintf(q,200,"DBGRM3+64:");
          for(int i=64;i<128;i++){ q+=snprintf(q,12," %08X",w[i]); if(i%16==15){openrfg_log(b); q=b; q+=snprintf(q,20,"DBGRM3+%d:",i+1);} }
          const unsigned* v=(const unsigned*)entityB; q=b; q+=snprintf(q,200,"DBGRM4+64:");
          for(int i=64;i<128;i++){ q+=snprintf(q,12," %08X",v[i]); if(i%16==15){openrfg_log(b); q=b; q+=snprintf(q,20,"DBGRM4+%d:",i+1);} }
          { const unsigned* c=(const unsigned*)agent->m_collidable[0]; q=b; q+=snprintf(q,200,"DBGRM5 own=%d cAwords-8..+23:",(int)agent->m_collidable[0]->m_ownerOffset);
            for(int i=-8;i<24;i++) q+=snprintf(q,12," %08X",c[i]); openrfg_log(b);
            const unsigned* e=(const unsigned*)entityA; q=b; q+=snprintf(q,200,"DBGRM6 eA words0..31:"); for(int i=0;i<32;i++) q+=snprintf(q,12," %08X",e[i]); openrfg_log(b); }
          { hkpWorld* w=entityA->m_world; int where=-1,wb=-1; int nAct=w->m_activeSimulationIslands.getSize(), nIn=w->m_inactiveSimulationIslands.getSize();
            for(int pass=0;pass<3;pass++){ int n= pass==0? nAct : pass==1? nIn : 1;
              for(int i=0;i<n;i++){ hkpSimulationIsland* is= pass==0? w->m_activeSimulationIslands[i] : pass==1? w->m_inactiveSimulationIslands[i] : w->m_fixedIsland; if(!is) continue;
                for(int j=0;j<is->m_entities.getSize();j++){ if(is->m_entities[j]==entityA) where=pass*1000+i; if(is->m_entities[j]==entityB) wb=pass*1000+i; } } }
            snprintf(b,sizeof b,"DBGRM7 world=%p nAct=%d nInact=%d fixedIsl=%p eAfoundIn=%d eBfoundIn=%d motionTypeA=%d motionTypeB=%d btypeA=%d btypeB=%d",(void*)w,nAct,nIn,(void*)w->m_fixedIsland,where,wb,(int)entityA->m_motion.m_type.m_storage,(int)entityB->m_motion.m_type.m_storage,(int)agent->m_collidable[0]->m_broadPhaseHandle.m_type,(int)agent->m_collidable[1]->m_broadPhaseHandle.m_type); openrfg_log(b); }
          (void)offB; }
    }
#endif
    hkpAgentNnTrack& track = hkWorldAgentUtil_track(theIsland, agent->m_nnTrackType.m_storage);
    hkpContactMgr* mgr = agent->m_contactMgr;
    hkAgentNnMachine_DestroyAgent(track, agent, theIsland->m_world->m_collisionDispatcher, *theIsland);
    mgr->cleanup();
}

// 0x00E1E160
void hkpWorldAgentUtil::removeAgentAndItsToiEvents(hkpAgentNnEntry* agent)
{
    hkWorldAgentUtil_entity(agent->m_collidable[0])->m_simulationIsland->m_world->m_simulation->removeCollisionInformationForAgent(agent);
    removeAgent(agent);
}
