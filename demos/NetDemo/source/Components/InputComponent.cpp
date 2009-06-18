/*
* Copyright, 2009, Alion Science and Technology Corporation, all rights reserved.
*
* See the .h file for complete licensing information.
*
* Alion Science and Technology Corporation
* 5365 Robin Hood Road
* Norfolk, VA 23513
* (757) 857-5670, www.alionscience.com
*
 David Guthrie
*/
#include <dtUtil/mswin.h>
#include <osgGA/GUIEventAdapter>

#include <dtGame/messagetype.h>
#include <dtGame/actorupdatemessage.h>
#include <dtABC/application.h>
#include <dtActors/engineactorregistry.h>
#include <dtActors/playerstartactorproxy.h>
#include <dtActors/gamemeshactor.h>
#include <dtPhysics/physicsmaterialactor.h>
#include <dtPhysics/physicscomponent.h>
#include <dtPhysics/bodywrapper.h>
#include <dtCore/deltawin.h>
#include <dtCore/shadermanager.h>
#include <dtCore/shaderprogram.h>

#include <SimCore/BaseGameEntryPoint.h>
#include <SimCore/Utilities.h>
#include <SimCore/ClampedMotionModel.h>
#include <SimCore/Messages.h>
#include <SimCore/MessageType.h>
#include <SimCore/Components/GameState/GameStateChangeMessage.h>

#include <States.h>
#include <ActorRegistry.h>
#include <Components/InputComponent.h>
#include <Actors/PlayerStatusActor.h>
// TEMP STUFF FOR VEHICLE
#include <Actors/HoverVehicleActor.h>
#include <Actors/HoverVehiclePhysicsHelper.h>
#include <Actors/EnemyMine.h>

#include <osg/io_utils>
#include <iostream>
#include <osgSim/DOFTransform>
#include <dtUtil/nodecollector.h>

namespace NetDemo
{
   const dtUtil::RefString InputComponent::DOF_NAME_WEAPON_PIVOT("dof_gun_01");
   const dtUtil::RefString InputComponent::DOF_NAME_WEAPON_FIRE_POINT("dof_hotspot_01");
   const dtUtil::RefString InputComponent::DOF_NAME_RINGMOUNT("dof_turret_01");
   const dtUtil::RefString InputComponent::DOF_NAME_VIEW_01("dof_view_01");
   const dtUtil::RefString InputComponent::DOF_NAME_VIEW_02("dof_view_02");

   //////////////////////////////////////////////////////////////
   InputComponent::InputComponent(const std::string& name)
      : SimCore::Components::BaseInputComponent(name)
      , mIsInGameState(false)
   {

   }

   //////////////////////////////////////////////////////////////
   InputComponent::~InputComponent()
   {

   }

   //////////////////////////////////////////////////////////////
   void  InputComponent::ProcessMessage(const dtGame::Message& message)
   {
      const dtGame::MessageType& msgType = message.GetMessageType();
      if (msgType == dtGame::MessageType::INFO_PLAYER_ENTERED_WORLD
               && message.GetSource() == GetGameManager()->GetMachineInfo())
      {
         SimCore::Actors::StealthActorProxy* stealthProxy;
         GetGameManager()->FindGameActorById(message.GetAboutActorId(), stealthProxy);
         if (stealthProxy == NULL)
         {
            LOG_ERROR("Got a player entered world message, but no player was found.")
            return;
         }
         else if (!stealthProxy->IsRemote()) // Somebody else's player.
         {
            SetStealthActor(static_cast<SimCore::Actors::StealthActor*>(&stealthProxy->GetGameActor()));
            GetStealthActor()->SetAttachAsThirdPerson(true);

            // We start with observer motion model. When we detach from vehicles, we go back to this. 
            // When we attach to vehicles, this gets trampled.
            mMotionModel->SetTarget(&stealthProxy->GetGameActor());
            EnableMotionModels();
         }
      }
      else if (dtGame::MessageType::INFO_ACTOR_UPDATED == msgType)
      {
         HandleActorUpdateMessage(message);
      }
      else if (SimCore::MessageType::GAME_STATE_CHANGED == msgType)
      {
         HandleStateChangeMessage(static_cast<const SimCore::Components::GameStateChangedMessage&>(message));
      }
      else if (dtGame::MessageType::INFO_MAP_LOADED == msgType)
      {
      }
      else if (dtGame::MessageType::TICK_LOCAL == msgType)
      {
         // Disable motion models if we lost firepower - this should be done elsewhere, by a message or something
         if(mVehicle.valid() && mVehicle->IsFirepowerDisabled())
         {
            if (mRingMM->IsEnabled())
               mRingMM->SetEnabled(false);
            if (mWeaponMM->IsEnabled())
               mWeaponMM->SetEnabled(false);
            //mWeapon->SetTriggerHeld( false );
         }
      }
      // Actor was deleted. Clear out our stealth or vehicle if appropriate
      else if (dtGame::MessageType::INFO_ACTOR_DELETED == msgType)
      {
         const dtCore::UniqueId& id = message.GetAboutActorId();
         SimCore::Actors::StealthActor* stealth = GetStealthActor();
         if (stealth != NULL && stealth->GetUniqueId() == id)
         {
            SetStealthActor(NULL); // this is VERY VERY bad btw. We assume we are shutting down or something
         }
         else if (mVehicle.valid() && mVehicle->GetUniqueId() == id)
         {
            DetachFromCurrentVehicle();
         }
      }
   }

   //////////////////////////////////////////////////////////////////////////
   void InputComponent::HandleStateChangeMessage( const SimCore::Components::GameStateChangedMessage& stateChange )
   {
      const SimCore::Components::StateType& state = stateChange.GetNewState();

      if (state == NetDemoState::STATE_GAME_RUNNING)
      {
         mIsInGameState = true;
      }
      else
      {
         mIsInGameState = false;
      }
      EnableMotionModels();
   }

   //////////////////////////////////////////////////////////////
   void InputComponent::OnAddedToGM()
   {
      BaseClass::OnAddedToGM();
      dtABC::Application& app = GetGameManager()->GetApplication();
      mMotionModel = new dtCore::FlyMotionModel(app.GetKeyboard(),app.GetMouse(), dtCore::FlyMotionModel::OPTION_DEFAULT);

      mRingMM = new SimCore::ClampedMotionModel(app.GetKeyboard(), app.GetMouse());
      mRingMM->SetMaximumMouseTurnSpeed(40.0f);
      mRingMM->SetUpDownLimit(0.0f);
      mRingMM->SetName("RingMM");

      mWeaponMM = new SimCore::ClampedMotionModel(app.GetKeyboard(), app.GetMouse());
      mWeaponMM->SetLeftRightLimit(0.0f);
      mWeaponMM->SetMaximumMouseTurnSpeed(70.0f);
      mWeaponMM->SetUpDownLimit(45.0f, 15.0f); // should probably be per vehicle
      mWeaponMM->SetName("WeaponMM");

   }

   //////////////////////////////////////////////////////////////
   void InputComponent::OnRemovedFromGM()
   {
      BaseClass::OnRemovedFromGM();

      DetachFromCurrentVehicle();

      mMotionModel = NULL;
      mRingMM = NULL;
      mWeaponMM = NULL;
      mVehicle = NULL;
      mHelpers.clear();
      mAppComp = NULL;
      mDOFRing = NULL;
      mDOFWeapon = NULL;
      mRingMM = NULL;
      mWeaponMM = NULL;
   }

   ////////////////////////////////////////////////////////////////////
   void InputComponent::HandleActorUpdateMessage(const dtGame::Message& msg)
   {

      const dtGame::ActorUpdateMessage &updateMessage =
         static_cast<const dtGame::ActorUpdateMessage&> (msg);

      // PLAYER STATUS - if it's ours, then update our attached vehicle
      if (updateMessage.GetActorType() == NetDemoActorRegistry::PLAYER_STATUS_ACTOR_TYPE && 
         updateMessage.GetSource() == GetGameManager()->GetMachineInfo())
      {
         // Find the actor in the GM - assume not null, else we're doomed anyway.
         PlayerStatusActorProxy *playerProxy;
         GetGameManager()->FindGameActorById(updateMessage.GetAboutActorId(), playerProxy);
         PlayerStatusActor &playerStatus = playerProxy->GetActorAsPlayerStatus();

         // If we don't have a vehicle yet, or our current vehicle is different, then attach to it.
         if (!mVehicle.valid() || mVehicle->GetUniqueId().ToString() != playerStatus.GetAttachedVehicleID())
         {
            // Find our vehicle - we assume it exists... if not, we'll crash
            SimCore::Actors::BasePhysicsVehicleActorProxy* vehicleProxy;
            GetGameManager()->FindActorById(dtCore::UniqueId(playerStatus.GetAttachedVehicleID()), vehicleProxy);
            if (vehicleProxy != NULL)
            {
               SimCore::Actors::BasePhysicsVehicleActor* vehicle = 
                  dynamic_cast<SimCore::Actors::BasePhysicsVehicleActor*>(vehicleProxy->GetActor());
               AttachToVehicle(vehicle);
            }
            else // no vehicle to attach to, so just detach
            {
               DetachFromCurrentVehicle();
            }

         }

      }

   }

   //////////////////////////////////////////////////////////////
   bool InputComponent::HandleKeyPressed(const dtCore::Keyboard* keyboard, int key)
   {
      bool keyUsed = true;
      switch(key)
      {
         case '\n':
         case '\r':
         case 'u':
         {
            FireSomething();
            break;
         }
         case 'r':
         {
            DoRayCast();
            break;
         }

         case 'p':
         {
            if (SimCore::Utils::IsDevModeOn(*GetGameManager())) 
            {
               dtCore::ShaderManager::GetInstance().ReloadAndReassignShaderDefinitions("Shaders/ShaderDefs.xml");
               //ToggleEntityShaders();
               LOG_ALWAYS("Reloading All Shaders...");
            }
            break;
         }

         case '5':
            {
               /////////////////////////////////////////////////////////
               LOG_ALWAYS("TEST - HACK - Attempting to create vehicle!!! ");
               // Hack stuff - add a vehicle here. For testing purposes.  
               dtCore::RefPtr<HoverVehicleActorProxy> testVehicleProxy = NULL;
               SimCore::Utils::CreateActorFromPrototypeWithException(*GetGameManager(), 
                  "Hover Vehicle", testVehicleProxy, "Check your additional maps in config.xml (compare to config_example.xml).");
               HoverVehicleActor *vehicleActor = dynamic_cast<HoverVehicleActor*>(&testVehicleProxy->GetGameActor());
               vehicleActor->SetHasDriver(true);
               GetGameManager()->AddActor(*testVehicleProxy, false, true);

               break;
            }

         case 't':
            {
               /////////////////////////////////////////////////////////
               LOG_ALWAYS("TEST - HACK - CREATING TARGET!!! ");
               // Hack stuff - add a vehicle here. For testing purposes.  
               dtCore::RefPtr<EnemyMineActorProxy> testEnemyMine = NULL;
               SimCore::Utils::CreateActorFromPrototypeWithException(*GetGameManager(), 
                  "Enemy Mine Prototype", testEnemyMine, "Check your additional maps in config.xml (compare to config_example.xml).");
               GetGameManager()->AddActor(*testEnemyMine, false, true);

               break;
            }

         case osgGA::GUIEventAdapter::KEY_Insert:
            {
               std::string developerMode;
               developerMode = GetGameManager()->GetConfiguration().GetConfigPropertyValue
                  (SimCore::BaseGameEntryPoint::CONFIG_PROP_DEVELOPERMODE, "false");
               if (developerMode == "true" || developerMode == "1")
               {
                  GetGameManager()->GetApplication().SetNextStatisticsType();
               }
            }
            break;

         case 'o':
            {
               // Go forward 5 mins in time
               IncrementTime(+5);
            }
            break;

         case 'i':
            {
               // go back 5 mins in time
               IncrementTime(-5);
            }
            break;

         case osgGA::GUIEventAdapter::KEY_Escape:
            {
               // Escapce key should act as one would expect, to escape from the
               // program in some manner, even if it means going through the menu system.
               GetAppComponent()->DoStateTransition(&Transition::TRANSITION_BACK);
            }
            break;

         case osgGA::GUIEventAdapter::KEY_Tab:
            {
               dtABC::Application& app = GetGameManager()->GetApplication();
               app.GetWindow()->SetFullScreenMode(!app.GetWindow()->GetFullScreenMode());
            }
            break;

         default:
            keyUsed = false;
      }

      if(!keyUsed)
         return BaseClass::HandleKeyPressed(keyboard, key);
      else 
         return keyUsed;
   }

   //////////////////////////////////////////////////////////////
   void InputComponent::DoRayCast()
   {
      dtCore::Transform xform;
      GetGameManager()->GetApplication().GetCamera()->GetTransform(xform);
      osg::Matrix m;
      xform.Get(m);
      dtPhysics::VectorType rayDir(m(1, 0), m(1, 1), m(1, 2));

      dtPhysics::RayCast ray;
      dtPhysics::RayCast::Report report;

      ray.SetOrigin(xform.GetTranslation());
      ray.SetDirection(rayDir * 500);

      dtPhysics::PhysicsWorld::GetInstance().TraceRay(ray, report);

      if (report.mHasHitObject)
      {
         std::cout << "Ray Hit at position: " << report.mHitPos << "  Distance: " << report.mDistance << std::endl;
      }
      else
      {
         std::cout << "No Ray Hit." << std::endl;
      }
   }

   //////////////////////////////////////////////////////////////
   void InputComponent::FireSomething()
   {
      dtPhysics::MaterialActorProxy* projectileMaterial = NULL;
      static const dtUtil::RefString PROJECTILE_MATERIAL_NAME("Projectile Material");
      GetGameManager()->FindActorByName(PROJECTILE_MATERIAL_NAME, projectileMaterial);

      if (projectileMaterial == NULL)
      {
         LOG_ERROR("Can't create a projectile, the material is NULL");
         return;
      }

      dtActors::GameMeshActorProxy* projectilePrototype = NULL;
      static const dtUtil::RefString PROJECTILE_CRATE_NAME("Crate");
      GetGameManager()->FindPrototypeByName(PROJECTILE_CRATE_NAME, projectilePrototype);

      if (projectilePrototype == NULL)
      {
         LOG_ERROR("Can't create a projectile, the prototype NULL");
         return;
      }

      dtCore::RefPtr<dtActors::GameMeshActorProxy> projectile = NULL;
      GetGameManager()->CreateActorFromPrototype(projectilePrototype->GetId(), projectile);

      projectile->SetName("Silly Crate");

      dtPhysics::PhysicsComponent* physicsComponent = NULL;
      GetGameManager()->GetComponentByName(dtPhysics::PhysicsComponent::DEFAULT_NAME, physicsComponent);
      if (physicsComponent == NULL)
      {
         LOG_ERROR("No Physics Component was found.");
         return;
      }

      dtCore::RefPtr<dtPhysics::PhysicsHelper> helper = new dtPhysics::PhysicsHelper(*projectile);
      mHelpers.push_back(helper);

      helper->SetMaterialActor(projectileMaterial);
      dtCore::RefPtr<dtPhysics::PhysicsObject> physicsObject = new dtPhysics::PhysicsObject();
      helper->AddPhysicsObject(*physicsObject);
      physicsObject->SetName(projectile->GetName());
      //physicsObject->SetPrimitiveType(dtPhysics::PrimitiveType::CONVEX_HULL);
      physicsObject->SetPrimitiveType(dtPhysics::PrimitiveType::BOX);
      physicsObject->SetMass(3.0f);
      physicsObject->SetMechanicsType(dtPhysics::MechanicsType::DYNAMIC);
      physicsObject->SetExtents(osg::Vec3(1.0f, 1.0f, 1.0f));
      //physicsObject->CreateFromProperties(projectile->GetActor()->GetOSGNode());
      //physicsObject->SetActive(true);

      dtCore::Transformable* actor;
      projectile->GetActor(actor);
      dtCore::Transform xform;
      xform.MakeIdentity();
      actor->SetTransform(xform);
      physicsObject->CreateFromProperties(actor->GetOSGNode());
      physicsObject->SetActive(true);


      //dtCore::Transform xform;
      GetGameManager()->GetApplication().GetCamera()->GetTransform(xform);
      osg::Matrix m;
      xform.Get(m);
      dtPhysics::VectorType force(100 * m(1, 0), 100 * m(1, 1), 100 * m(1, 2));
      physicsObject->GetBodyWrapper()->ApplyImpulse(force);

      physicsObject->SetTransform(xform);
      //dtCore::Transformable* actor;
      //projectile->GetActor(actor);
      actor->SetTransform(xform);

      GetGameManager()->AddActor(*projectile, false, true);

      physicsComponent->RegisterHelper(*helper);
   }

   /////////////////////////////////////////////////////////////////////////////
   GameLogicComponent* InputComponent::GetAppComponent()
   {
      if( ! mAppComp.valid() )
      {
         GameLogicComponent* comp = NULL;
         GetGameManager()->GetComponentByName( GameLogicComponent::DEFAULT_NAME, comp );
         mAppComp = comp;
      }

      if( ! mAppComp.valid() )
      {
         LOG_ERROR( "Input Component cannot access the Game App Component." );
      }

      return mAppComp.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   void InputComponent::AttachToVehicle(SimCore::Actors::BasePhysicsVehicleActor *vehicle)
   {
      DetachFromCurrentVehicle();

      mVehicle = vehicle;
      if (vehicle == NULL) return;

      // NOTE - The camera sits at the bottom of a VERY large hierarchy of DoF's. Looks like this:
      //     Vehicle (center of vehicle)
      //       - Ring Mount (often swivels left/right)
      //           - mDoFWeapon (pivots about weapon pivot point)
      //               - mWeapon (3D model of weapon)
      //                   - mWeaponEyePoint (offset for human eyepoint)
      //                       - StealthActor (yay!  almost there)
      //                           - camera

      // Get the DOF's
      mDOFRing = mVehicle->GetNodeCollector()->GetDOFTransform(DOF_NAME_RINGMOUNT.Get());
      // Check for DOF's - toss an error or something
      if (!mDOFRing.valid())
      {
         LOG_ERROR("CRITICAL ERROR attaching to vehicle[" + vehicle->GetName() + "]. No DOF[" + DOF_NAME_RINGMOUNT.Get() + "]");
         return;
      }
      mDOFWeapon = mVehicle->GetNodeCollector()->GetDOFTransform(DOF_NAME_WEAPON_PIVOT.Get());
      if (!mDOFWeapon.valid())
      {
         LOG_ERROR("CRITICAL ERROR attaching to vehicle[" + mVehicle->GetName() + "]. No DOF[" + DOF_NAME_WEAPON_PIVOT.Get() + "]");
         return;
      }

      //AttachToView(DOF_NAME_VIEW_DEFAULT.Get());
      mMotionModel->SetTarget(NULL);
      GetStealthActor()->AttachOrDetachActor(&mVehicle->GetGameActorProxy(), 
         mVehicle->GetUniqueId(), DOF_NAME_WEAPON_PIVOT.Get());
      //dtCore::RefPtr<SimCore::AttachToActorMessage> msg;
      //GetGameManager()->GetMessageFactory().CreateMessage(SimCore::MessageType::ATTACH_TO_ACTOR, msg);
      //msg->SetAboutActorId(GetStealthActor()->GetUniqueId());
      //msg->SetAttachToActor(mVehicle->GetUniqueId());
      //msg->SetAttachPointNodeName(DOF_NAME_WEAPON_PIVOT.Get());
      //GetGameManager()->SendMessage(*msg.get());

      ///////////////////////////////////////////
      // Setup our Motion Models
      mWeaponMM->SetTargetDOF(mDOFWeapon.get());

      // Look up a weird quirk of the data (only set on the hover vehicle actor).
      // Is the turret 'hard-wired' to the vehicle? If so, the ring mount motion
      // model turns the vehicle, not just the ring mount DoF. This is true for
      // instance, with the Hover Vehicle.
      if(IsVehiclePivotable())
         mRingMM->SetTarget(mVehicle.get());
      else
         mRingMM->SetTargetDOF(mDOFRing.get());

      // Tell the MotionModels about the articulation helper - This allows the artic helper to
      // know about data changes which in turn, allows it to check for changes before publishing an update.
      mRingMM->SetArticulationHelper(mVehicle->GetArticulationHelper());
      mWeaponMM->SetArticulationHelper(mVehicle->GetArticulationHelper());

      EnableMotionModels();

      //dtCore::RefPtr<dtUtil::NodePrintOut> nodePrinter = new dtUtil::NodePrintOut();
      //std::string nodes = nodePrinter->CollectNodeData(*vehicle.GetNonDamagedFileNode());
      //std::cout << " --------- NODE PRINT OUT FOR VEHICLE --------- " << std::endl;
      //std::cout << nodes.c_str() << std::endl;
   }

   /////////////////////////////////////////////////////////////////////////////
   void InputComponent::DetachFromCurrentVehicle()
   {
      if (mVehicle.valid())
      {
         //mDOFWeapon->removeChild(GetStealthActor()->GetOSGNode());
         mWeaponMM->SetTargetDOF(NULL);
         mRingMM->SetTarget(NULL);
         mRingMM->SetTargetDOF(NULL);

         GetStealthActor()->AttachOrDetachActor(NULL, dtCore::UniqueId(""));
         //if (GetStealthActor()->IsAttachedToActor())
         //   GetStealthActor()->DoDetach();
         // Re-enable our base motion model - so we have something
         mMotionModel->SetTarget(GetStealthActor());
      }

      mVehicle = NULL;
      EnableMotionModels();
   }

   ////////////////////////////////////////////////////////////////////////////////
   bool InputComponent::IsVehiclePivotable()
   {
      HoverVehicleActor* hoverActor = dynamic_cast<HoverVehicleActor*>(mVehicle.get());
      return hoverActor != NULL && hoverActor->GetVehicleIsTurret();
   }

   ////////////////////////////////////////////////////////////////////////////////
   void InputComponent::EnableMotionModels()
   {
      if (!mIsInGameState || !mVehicle.valid())
      {
         mWeaponMM->SetEnabled(false);
         mRingMM->SetEnabled(false);

         mMotionModel->SetEnabled(true);
      }
      else //if (mVehicle.valid()) //  Attached
      {
         bool enableVehicleModels = !mVehicle->IsFlamesPresent();
         mWeaponMM->SetEnabled(enableVehicleModels);
         mRingMM->SetEnabled(enableVehicleModels);

         mMotionModel->SetEnabled(false);
      }
   }
}

