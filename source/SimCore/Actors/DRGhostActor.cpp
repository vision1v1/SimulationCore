/* -*-c++-*-
 * Driver Demo - HoverVehicleActor (.cpp & .h) - Using 'The MIT License'
 * Copyright (C) 2008, Alion Science and Technology Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Curtiss Murphy
 */
#include <prefix/SimCorePrefix-src.h>

#include <SimCore/Actors/DRGhostActor.h>

#include <dtCore/shadermanager.h>

#include <dtGame/deadreckoningcomponent.h>
#include <dtGame/deadreckoninghelper.h>
#include <dtGame/basemessages.h>
#include <osg/Switch>
#include <osgSim/DOFTransform>


// For alpha
#include <osg/BlendFunc>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/MatrixTransform>

///////////////////////
// For the particle system visitor. Delete once merged back to the trunk and SetEnable no longer has Freeze
#include <dtCore/particlesystem.h>
#include <osg/Group>
#include <osg/NodeVisitor>
// Delete the above when we merge back to the trunk and SetEnable is no longer has Freeze
///////////////////////

#include <osg/Array>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Switch>
#include <osg/Stateset>

#include <SimCore/Actors/BaseEntity.h>
#include <SimCore/Actors/Platform.h>

#include <iostream>

namespace SimCore
{
   namespace Actors
   {

      ///  DUPLICATE FROM PARTICLESYSTEM.CPP. DELETE ONCE BACK ON THE TRUNK AND Freeze 
      ///  IS NOT PART OF SetEnable()
      /**
      * A visitor class that applies a set of particle system parameters.
      */
      class ParticleSystemParameterVisitor : public osg::NodeVisitor
      {
      public:
         ParticleSystemParameterVisitor(bool enabled)
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN),
            mEnabled(enabled)
         {}

         virtual void apply(osg::Node& node)
         {
            osg::Node* nodePtr = &node;
            if (osgParticle::Emitter* emitter =
               dynamic_cast<osgParticle::Emitter*>(nodePtr))
            {
               emitter->setEnabled(mEnabled);
            }
            traverse(node);
         }

      private:
         bool mEnabled;
      };


      ///////////////////////////////////////////////////////////////////////////////////
      DRGhostActor::DRGhostActor(DRGhostActorProxy& proxy)
      : dtActors::GameMeshActor(proxy)
      , mSlaveUpdatedParticleIsActive(false)
      , mVelocityArrowDrawScalar(1.0f)
      , mVelocityArrowColor(0.2f, 0.2f, 1.0f)
      , mVelocityArrowMaxNumVelTrails(10)
      , mVelocityArrowCurrentVelIndex(0)
      , mVelocityArrowDrawOnNextFrame(false)
      {
      }

      ///////////////////////////////////////////////////////////////////////////////////
      DRGhostActor::~DRGhostActor(void)
      {
         if (mSlavedEntity.valid())
         {
            // See Baseentity::OnEnteredWorld for more info on why we do this
            //mSlavedEntity->GetDeadReckoningHelper().SetMaxTranslationSmoothingTime
            //   (dtGame::DeadReckoningHelper::DEFAULT_MAX_SMOOTHING_TIME_POS);
            //mSlavedEntity->GetDeadReckoningHelper().SetMaxRotationSmoothingTime
            //   (dtGame::DeadReckoningHelper::DEFAULT_MAX_SMOOTHING_TIME_ROT);
         }

      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActor::CleanUp()
      {
         // Remove our velocity line node from the scene before we go.
         dtGame::IEnvGameActorProxy *envProxy = GetGameActorProxy().GetGameManager()->GetEnvironmentActor();
         if (mVelocityParentNode.valid() && envProxy != NULL)
         {
            dtGame::IEnvGameActor *envActor;
            envProxy->GetActor(envActor);
            envActor->RemoveActor(*mVelocityParentNode);//GetMatrixNode()->removeChild(mVelocityParentNode);
         }
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActor::SetSlavedEntity(SimCore::Actors::BaseEntity* newEntity)
      {
         mSlavedEntity = newEntity;
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActor::SetVelocityArrowMaxNumVelTrails(unsigned int newValue)
      {
         if (mVelocityParentNode.valid())
         {
            LOG_ERROR("You cannot set the number of velocity trails AFTER the ghost has been added with GM.AddActor().");
         }
         else
         {
            mVelocityArrowMaxNumVelTrails = newValue;
         }
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActor::OnEnteredWorld()
      {

         if(!IsRemote())
         {
            // Get our mesh from the entity we are modeling
            if (mSlavedEntity.valid())
            {
               SimCore::Actors::Platform *platform = dynamic_cast<SimCore::Actors::Platform*>(mSlavedEntity.get());
               if (platform != NULL)
               {
                  SetMesh(platform->GetNonDamagedNodeFileName());
               }
               //mSlavedEntity->GetDeadReckoningHelper().SetMaxRotationSmoothingTime(dtGame::DeadReckoningHelper::DEFAULT_MAX_SMOOTHING_TIME_ROT);
               //mSlavedEntity->GetDeadReckoningHelper().SetMaxTranslationSmoothingTime(dtGame::DeadReckoningHelper::DEFAULT_MAX_SMOOTHING_TIME_POS);
            }
            SetShaderGroup("GhostVehicleShaderGroup");

            osg::Group* g = GetOSGNode()->asGroup();
            osg::StateSet* ss = g->getOrCreateStateSet();
            ss->setMode(GL_BLEND,osg::StateAttribute::ON);
            dtCore::RefPtr<osg::BlendFunc> trans = new osg::BlendFunc();
            trans->setFunction( osg::BlendFunc::SRC_ALPHA ,osg::BlendFunc::ONE_MINUS_SRC_ALPHA );
            ss->setAttributeAndModes(trans.get());
            ss->setRenderingHint( osg::StateSet::TRANSPARENT_BIN );

            UpdateOurPosition();

            // Add a particle system to the Ghost to see where it's been.
            mTrailParticles = new dtCore::ParticleSystem;
            mTrailParticles->LoadFile("Particles/SimpleSpotTrail.osg", true);
            mTrailParticles->SetEnabled(true);
            AddChild(mTrailParticles.get());


            // Add a particle system to the Ghost - that works ONLY right after our slave gets updated.
            mUpdateTrailParticles = new dtCore::ParticleSystem;
            mUpdateTrailParticles->LoadFile("Particles/SimpleSpotTrailRed.osg", true);
            AddChild(mUpdateTrailParticles.get());
            // Start our red system OFF. 
            //mUpdateTrailParticles->SetEnabled(false); // put back when ParticleSystemParameterVisitor is deleted
            ParticleSystemParameterVisitor pspv = ParticleSystemParameterVisitor(false);
            mUpdateTrailParticles->GetOSGNode()->accept(pspv);
            mSlaveUpdatedParticleIsActive = false;

            // Replace the ghost shader with a simple shader that uses no lighting (ie fully lit)
            dtCore::RefPtr<dtCore::ShaderProgram> shader = 
               dtCore::ShaderManager::GetInstance().FindShaderPrototype("GhostParticleShader", "GhostVehicleShaderGroup");
            if(!shader.valid()) 
            {
               LOG_ERROR("FAILED to load shader for Ghost Particles. Will not correctly visualize update particles.");
            }
            else
            {
               dtCore::ShaderManager::GetInstance().AssignShaderFromPrototype(*shader, *mTrailParticles->GetOSGNode());
               dtCore::ShaderManager::GetInstance().AssignShaderFromPrototype(*shader, *mUpdateTrailParticles->GetOSGNode());
            }

            SetupVelocityLine();
         }

         BaseClass::OnEnteredWorld();
      }

      //////////////////////////////////////////////////////////////////////
      void DRGhostActor::SetupVelocityLine()
      {
         mVelocityParentNode = new dtCore::Transformable();//osg::Group();

         // Create a velocity pointer.
         mVelocityArrowGeode = new osg::Geode();
         mVelocityArrowGeom = new osg::Geometry();
         mVelocityArrowVerts = new osg::Vec3Array();
         // 2 points to create a line for our velocity.
         mVelocityArrowVerts->reserve(mVelocityArrowMaxNumVelTrails * 2);
         for(unsigned int i = 0; i < mVelocityArrowMaxNumVelTrails; ++i)
         {
            mVelocityArrowVerts->push_back(osg::Vec3(0.0f, 0.0f, 0.0f));
            mVelocityArrowVerts->push_back(osg::Vec3(0.0f, 0.0f, 0.0f));
         }

         mVelocityArrowGeom->setUseDisplayList(false);
         mVelocityArrowGeom->setVertexArray(mVelocityArrowVerts.get());
         mVelocityArrowGeode->addDrawable(mVelocityArrowGeom.get());
         osg::StateSet* ss(NULL);
         ss = mVelocityArrowGeode->getOrCreateStateSet();
         ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

         mVelocityArrowGeom->setColorBinding(osg::Geometry::BIND_OVERALL);
         // Note - for future - if the color changes on the actor, need to reset this.
         osg::Vec3Array* colors = new osg::Vec3Array;
         colors->push_back(mVelocityArrowColor);
         mVelocityArrowGeom->setColorArray(colors); 

         mVelocityArrowGeom->setVertexArray(mVelocityArrowVerts);
         mVelocityArrowGeom->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, 2 * mVelocityArrowMaxNumVelTrails));

         // We put all of our velocity arrows under a special node that is NOT a child 
         // of the slave OR the ghost. The parent is world relative and doesn't move.
         mVelocityParentNode->GetMatrixNode()->addChild(mVelocityArrowGeode.get()); // GetMatrixNode()->addChild()
         dtGame::IEnvGameActorProxy *envProxy = GetGameActorProxy().GetGameManager()->GetEnvironmentActor();
         if (envProxy != NULL)
         {
            dtGame::IEnvGameActor *envActor;
            envProxy->GetActor(envActor);
            envActor->AddActor(*mVelocityParentNode);

            // Make this a settable value.
            dtCore::Transform xform(0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f);
            mVelocityParentNode->SetTransform(xform);
             
         }
         else 
         {
            LOG_ERROR("There is no environment actor - The DRGhost will not function correctly.");
         }
      }

      //////////////////////////////////////////////////////////////////////
      void DRGhostActor::OnTickLocal( const dtGame::TickMessage& tickMessage )
      {
         BaseClass::OnTickLocal( tickMessage );

         // Move to TickRemote().
         UpdateOurPosition();

         // Update our Velocity line.
         if (mVelocityArrowDrawOnNextFrame && mSlavedEntity.valid())
         {
            mVelocityArrowDrawOnNextFrame = false;

            dtCore::Transform xform;
            GetTransform(xform);
            osg::Vec3 ghostPos = xform.GetTranslation();

            osg::Vec3 velocity = mSlavedEntity->GetLastKnownVelocity();
            osg::Vec3Array* vertices = (osg::Vec3Array*)mVelocityArrowGeom->getVertexArray();
            // Set our start point to be our current ghost position
            unsigned int curIndex = mVelocityArrowCurrentVelIndex * 2;
            (*vertices)[curIndex].x() = ghostPos.x();
            (*vertices)[curIndex].y() = ghostPos.y(); 
            (*vertices)[curIndex].z() = ghostPos.z(); 

            // Set our end point to be current ghost pos + velocity.
            (*vertices)[curIndex + 1].x() = ghostPos.x() + velocity.x() * mVelocityArrowDrawScalar;
            (*vertices)[curIndex + 1].y() = ghostPos.y() + velocity.y() * mVelocityArrowDrawScalar;
            (*vertices)[curIndex + 1].z() = ghostPos.z() + velocity.z() * mVelocityArrowDrawScalar;
            mVelocityArrowCurrentVelIndex = (mVelocityArrowCurrentVelIndex + 1) % mVelocityArrowMaxNumVelTrails;

            // Make sure that we force a redraw and bounds update so we see our new verts.
            mVelocityArrowGeom->dirtyDisplayList();
            mVelocityArrowGeom->dirtyBound();
         }



         // If we previously activated our 'slave entity updated particle system', then turn it off
         mPosUpdatedParticleCountdown --;
         if (mPosUpdatedParticleCountdown < 0 && mSlaveUpdatedParticleIsActive)
         {
            //mUpdateTrailParticles->SetEnabled(false); // put back when ParticleSystemParameterVisitor is deleted
            mSlaveUpdatedParticleIsActive = false;
            ParticleSystemParameterVisitor pspv = ParticleSystemParameterVisitor(false);
            mUpdateTrailParticles->GetOSGNode()->accept(pspv);
         }

         /*
         static float countDownToDebug = 1.0f;
         countDownToDebug -= tickMessage.GetDeltaSimTime();
         if (countDownToDebug < 0.0)
         {
            countDownToDebug = 1.0f;
            if (mSlavedEntity.valid())
            {
               std::cout << "GHOST - Vel[" << mSlavedEntity->GetDeadReckoningHelper().GetLastKnownVelocity() << "]." << std::endl;
            }
            else
            {
               LOG_ERROR("Ghost DR Actor [" + GetName() + "] has NO SLAVE!");
            }
         }
         */
      }

      //////////////////////////////////////////////////////////////////////
      void DRGhostActor::UpdateOurPosition()
      {
         if (mSlavedEntity.valid())
         {
            dtCore::Transform ourTransform;
            GetTransform(ourTransform);

            ourTransform.SetTranslation(mSlavedEntity->
                     GetDeadReckoningHelper().GetCurrentDeadReckonedTranslation());
            ourTransform.SetRotation(mSlavedEntity->
                     GetDeadReckoningHelper().GetCurrentDeadReckonedRotation());

            SetTransform(ourTransform);
         }
      }

      ////////////////////////////////////////////////////////////////////////////////////
      void DRGhostActor::ProcessMessage(const dtGame::Message& message)
      {
         if (message.GetMessageType() == dtGame::MessageType::INFO_ACTOR_UPDATED)
         {
            //const dtGame::ActorUpdateMessage &updateMessage =
            //   static_cast<const dtGame::ActorUpdateMessage&> (message);

            // When our slave entity got updated, we are going to turn on red particle shader for one frame.
            //mUpdateTrailParticles->SetEnabled(true); // put back when ParticleSystemParameterVisitor is deleted
            mSlaveUpdatedParticleIsActive = true;
            ParticleSystemParameterVisitor pspv = ParticleSystemParameterVisitor(true);
            mUpdateTrailParticles->GetOSGNode()->accept(pspv);
            mPosUpdatedParticleCountdown = 2; // 2 to make sure particles draw, even if we have a whacky frame hiccup.


            mVelocityArrowDrawOnNextFrame = true;
         }
      }

      //////////////////////////////////////////////////////////////////////
      // PROXY
      //////////////////////////////////////////////////////////////////////
      DRGhostActorProxy::DRGhostActorProxy()
      {
         SetClassName("DRGhostActor");
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActorProxy::BuildPropertyMap()
      {
         //static const dtUtil::RefString GROUP("DRGhost Props");
         BaseClass::BuildPropertyMap();
         //DRGhostActor& actor = *static_cast<DRGhostActor*>(GetActor());

         // Add properties
      }

      ///////////////////////////////////////////////////////////////////////////////////
      DRGhostActorProxy::~DRGhostActorProxy(){}
      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActorProxy::CreateActor()
      {
         SetActor(*new DRGhostActor(*this));
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActorProxy::OnEnteredWorld()
      {
         BaseClass::OnEnteredWorld();

         if (!IsRemote())
         {
            //So it's not a frame behind, it needs to happen on tick remote.  This is because the DeadReckoningComponent
            //ticks on Tick-Remote..
            //RegisterForMessages(dtGame::MessageType::TICK_REMOTE, dtGame::GameActorProxy::TICK_REMOTE_INVOKABLE);
            RegisterForMessages(dtGame::MessageType::TICK_LOCAL, dtGame::GameActorProxy::TICK_LOCAL_INVOKABLE);

            // Listen for actor updates on our slave entity.
            if (GetActorAsDRGhostActor().GetSlavedEntity() != NULL)
            {
               RegisterForMessagesAboutOtherActor(dtGame::MessageType::INFO_ACTOR_UPDATED, 
                  GetActorAsDRGhostActor().GetSlavedEntity()->GetUniqueId(), PROCESS_MSG_INVOKABLE);
            }
            else 
            {
               LOG_ERROR("Ghost DR Actor [" + GetName() + "] has NO SLAVE - cannot register for updates!");
            }
         }
      }

      ///////////////////////////////////////////////////////////////////////////////////
      void DRGhostActorProxy::OnRemovedFromWorld()
      {
         GetActorAsDRGhostActor().CleanUp();
      }
   }
} // namespace
