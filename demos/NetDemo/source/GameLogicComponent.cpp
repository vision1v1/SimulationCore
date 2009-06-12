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
* @author David Guthrie, Curtiss Murphy
*/

#include <dtCore/camera.h>
#include <dtCore/transform.h>
#include <dtABC/application.h>
#include <dtGame/messagetype.h>
#include <dtGame/actorupdatemessage.h>
#include <dtGame/gameactor.h>
#include <dtGame/gamemanager.h>
//#include <dtGame/message.h>
#include <dtGame/basemessages.h>
#include <dtNetGM/clientnetworkcomponent.h>
#include <dtNetGM/servernetworkcomponent.h>

//#include <dtActors/playerstartactorproxy.h>
#include <dtActors/engineactorregistry.h>

#include <SimCore/Actors/EntityActorRegistry.h>
#include <SimCore/Actors/PlayerActor.h>
#include <SimCore/Actors/TerrainActorProxy.h>
#include <SimCore/Components/GameState/GameStateChangeMessage.h>
#include <SimCore/MessageType.h>
#include <SimCore/Messages.h>
#include <SimCore/Utilities.h>

#include <GameLogicComponent.h>
#include <ActorRegistry.h>
#include <PlayerStatusActor.h>
#include <States.h>
#include <ServerGameStatusActor.h>

// Temp - delete this unless you are using COuts.
//#include <iostream>
#include <HoverVehicleActor.h>

namespace NetDemo
{
   const std::string GameLogicComponent::TIMER_UPDATE_TERRAIN("FORCE_UPDATE_OF_TERRAIN");

   //////////////////////////////////////////////////////////////////////////
   GameLogicComponent::GameLogicComponent(const std::string& name)
      : BaseClass(name)
      , mIsServer(false)
      , mIsConnectedToNetwork(false)
      , mStartTheGameOnNextGameRunning(false)
   { 
      mLogger = &dtUtil::Log::GetInstance("GameAppComponent.cpp");

      // Register application-specific states.
      AddState(&NetDemoState::STATE_CONNECTING);
      AddState(&NetDemoState::STATE_LOBBY);
      AddState(&NetDemoState::STATE_UNLOAD);
      AddState(&NetDemoState::STATE_GAME_RUNNING);
      AddState(&NetDemoState::STATE_GAME_READYROOM);
      AddState(&NetDemoState::STATE_GAME_DEAD);
      AddState(&NetDemoState::STATE_GAME_OPTIONS);
      AddState(&NetDemoState::STATE_GAME_QUIT);
      AddState(&NetDemoState::STATE_GAME_UNKNOWN);
      AddState(&NetDemoState::STATE_SCORE_SCREEN);
   }


   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::OnAddedToGM()
   {

      dtUtil::ConfigProperties& configParams = GetGameManager()->GetConfiguration();
      //const std::string role = configParams.GetConfigPropertyValue("dtNetGM.Role", "server");
      //int serverPort = dtUtil::ToType<int>(configParams.GetConfigPropertyValue("dtNetGM.ServerPort", "7329"));
      const std::string gameName = "NetDemo";// configParams.GetConfigPropertyValue("dtNetGM.GameName", "NetDemo");
      int gameVersion = 1; //dtUtil::ToType<int>(configParams.GetConfigPropertyValue("dtNetGM.GameVersion", "1"));
      //const std::string host = configParams.GetConfigPropertyValue("dtNetGM.ServerHost", "127.0.0.1");

      // We can't add components while in a tick message, so we add both components up front.
      // SERVER COMPONENT 
      dtCore::RefPtr<dtNetGM::ServerNetworkComponent> serverComp =
         new dtNetGM::ServerNetworkComponent(gameName, gameVersion);
      GetGameManager()->AddComponent(*serverComp, dtGame::GameManager::ComponentPriority::NORMAL);
      // CLIENT COMPONENT
      dtCore::RefPtr<dtNetGM::ClientNetworkComponent> clientComp =
         new dtNetGM::ClientNetworkComponent(gameName, gameVersion);
      GetGameManager()->AddComponent(*clientComp, dtGame::GameManager::ComponentPriority::NORMAL);

   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::ProcessMessage(const dtGame::Message& msg)
   {
      BaseClass::ProcessMessage(msg);

      const dtGame::MessageType& messageType = msg.GetMessageType();

      // Process game state changes.
      if (messageType == dtGame::MessageType::NETSERVER_ACCEPT_CONNECTION)
      {
         LOG_ALWAYS("The server accepted the connection request message.");
      }
      else if (messageType == SimCore::MessageType::GAME_STATE_CHANGED)
      {
         HandleStateChangeMessage(static_cast<const SimCore::Components::GameStateChangedMessage&>(msg));
      }
      else if (dtGame::MessageType::INFO_MAP_LOADED == msg.GetMessageType())
      {
         HandleMapLoaded();
      }
      else if (dtGame::MessageType::INFO_MAP_UNLOADED == msg.GetMessageType())
      {
         mCurrentTerrainDrawActor = NULL;
         mServerGameStatusProxy = NULL;
         DoStateTransition(&Transition::TRANSITION_FORWARD);
      }
      else if (dtGame::MessageType::INFO_ACTOR_UPDATED == msg.GetMessageType())
      {
         HandleActorUpdateMessage(msg);
      }
      else if (dtGame::MessageType::INFO_TIMER_ELAPSED == msg.GetMessageType())
      {
         HandleTimerElapsedMessage(msg);
      }

      // Something about Game State changing here
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::InitializePlayer()
   {
      dtCore::RefPtr<dtGame::GameActorProxy> ap;

      // Every player always has a player actor. On some apps, it is an overkill, but
      // we use it anyway, for consistency. It allows tools, position, ability to have an avatar, walk, run, jump, etc.
      //GetGameManager()->CreateActor(*SimCore::Actors::EntityActorRegistry::PLAYER_ACTOR_TYPE, ap);
      GetGameManager()->CreateActor(*NetDemo::NetDemoActorRegistry::PLAYER_STATUS_ACTOR_TYPE, ap);
      //mPlayerStatus = static_cast<SimCore::Actors::PlayerActor*>(ap->GetActor());
      mPlayerStatus = static_cast<NetDemo::PlayerStatusActor*>(ap->GetActor());
      // make the camera a child
      mPlayerStatus->AddChild(GetGameManager()->GetApplication().GetCamera());
      mPlayerStatus->SetName("Player (Unknown)");

      // Hack stuff - move this to UI - user selected and all that.
      mPlayerStatus->SetIsServer(mIsServer);
      mPlayerStatus->SetTerrainPreference("Terrain1");
      //mPlayerStatus->SetTerrainPreference("Terrains:Level_DriverDemo.ive");
      mPlayerStatus->SetTeamNumber(1);
      mPlayerStatus->SetPlayerStatus(PlayerStatusActor::PlayerStatusEnum::IN_LOBBY);

      GetGameManager()->AddActor(mPlayerStatus->GetGameActorProxy(), false, true);


      //////////////TEMP HACK
      // Set the starting position from a player start actor in the map.
      //dtActors::PlayerStartActorProxy* startPosProxy = NULL;
      //GetGameManager()->FindActorByType(*dtActors::EngineActorRegistry::PLAYER_START_ACTOR_TYPE, startPosProxy);
      //if (startPosProxy != NULL)
      //{
      //   dtCore::Transformable* actor = NULL;
      //   startPosProxy->GetActor(actor);
      //   dtCore::Transform xform;
      //   actor->GetTransform(xform);
      //   mPlayerStatus->SetTransform(xform);
      //}
      ////////////////////////
   }

   ////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandleActorUpdateMessage(const dtGame::Message& msg)
   {
      const dtGame::ActorUpdateMessage &updateMessage =
         static_cast<const dtGame::ActorUpdateMessage&> (msg);

      // PLAYER STATUS
      if (mIsServer && updateMessage.GetActorType() == NetDemoActorRegistry::PLAYER_STATUS_ACTOR_TYPE)
      {
         // Find the actor in the GM
         dtGame::GameActorProxy *gap = GetGameManager()->FindGameActorById(updateMessage.GetAboutActorId());
         if(gap == NULL) 
            return;
         PlayerStatusActor *statusActor = dynamic_cast<PlayerStatusActor*>(gap->GetActor());
         if(statusActor == NULL)  
            return;

         HandlePlayerStatusUpdated(statusActor);

      }

      // SERVER STATUS
      else if (!mIsServer && updateMessage.GetActorType() == NetDemoActorRegistry::SERVER_GAME_STATUS_ACTOR_TYPE)
      {
         // Find the actor in the GM
         dtGame::GameActorProxy *gap = GetGameManager()->FindGameActorById(updateMessage.GetAboutActorId());
         if(gap == NULL)  return;
         ServerGameStatusActor *serverStatus = static_cast<ServerGameStatusActor*>(gap->GetActor());

         // If not the server, do a print out...
         std::ostringstream oss;
         oss << "Server Status Updated: [" << serverStatus->GetGameStatus().GetName() << "], Wave[" << 
            serverStatus->GetWaveNumber() << "] Players[" << serverStatus->GetNumPlayers() << "] TimeLeft[" << 
            serverStatus->GetTimeLeftInCurState() << "], EnemiesKilt[" << serverStatus->GetNumEnemiesKilled() << "].";
         LOG_ALWAYS(oss.str());
      }
   }

   ////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandlePlayerStatusUpdated(PlayerStatusActor *statusActor)
   {
      // If it's a local actor and we are the server, then we need to create our terrain.
      // The terrain will be published to the clients. 
      if (!statusActor->IsRemote())
      {
         if (statusActor->GetIsServer() && statusActor->GetTerrainPreference() != mCurrentTerrainPrototypeName)
         {
            mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__,
               "Creating new terrain [%s] on the Server.", statusActor->GetTerrainPreference().c_str());
            mTerrainToLoad = statusActor->GetTerrainPreference();
            UnloadCurrentTerrain();
            mCurrentTerrainPrototypeName = mTerrainToLoad;
            mTerrainToLoad = "";
            LoadNewTerrain();
         }
      }

      // It's remote.
      else if (mIsServer && mServerGameStatusProxy.valid())
      {
         // get the count of player status actors in gm.
         std::vector<dtDAL::ActorProxy*> playerActors;
         GetGameManager()->FindActorsByType(*NetDemoActorRegistry::PLAYER_STATUS_ACTOR_TYPE, playerActors);
         mServerGameStatusProxy->GetActorAsGameStatus().SetNumPlayers(playerActors.size());

         // (todo - future) Calculate the number of teams
      }

   }

   ////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandleTimerElapsedMessage(const dtGame::Message& msg)
   {
      const dtGame::TimerElapsedMessage& timerMsg =
         static_cast<const dtGame::TimerElapsedMessage&>(msg);

      // Force an update of the terrain actor. This will pass the actor out to late-joining clients.
      if(TIMER_UPDATE_TERRAIN == timerMsg.GetTimerName() && mCurrentTerrainDrawActor.valid())
      {
         //mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__,"Sending out a terrain actor update.");
         mCurrentTerrainDrawActor->GetGameActorProxy().NotifyFullActorUpdate();
      }

   }

   ////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandleMapLoaded()
   {
      InitializePlayer();

      // if we are a client, we let the server know we are now ready to receive messages
      // We can't receive/send messages to the server until we do this. Do it AFTER the map is loaded.
      dtNetGM::ClientNetworkComponent *networkComponent = 
         dynamic_cast<dtNetGM::ClientNetworkComponent *>(mNetworkComp.get());

      if (mIsConnectedToNetwork && !mIsServer && networkComponent != NULL)
      {
         networkComponent->SendRequestConnectionMessage();
      }

      // Create the server game status actor
      if (mIsServer) 
      {
         dtCore::RefPtr<dtGame::GameActorProxy> ap;
         GetGameManager()->CreateActor(*NetDemo::NetDemoActorRegistry::SERVER_GAME_STATUS_ACTOR_TYPE, ap);
         mServerGameStatusProxy = dynamic_cast<ServerGameStatusActorProxy*> (ap.get());
         ServerGameStatusActor &gameStatus = mServerGameStatusProxy->GetActorAsGameStatus();
         gameStatus.SetNumTeams(1);
         gameStatus.SetNumPlayers(1); // we account for ourself already
         gameStatus.SetGameDifficulty(1);
         gameStatus.SetGameStatus(ServerGameStatusActor::ServerGameStatusEnum::WAVE_ABOUT_TO_START);
         gameStatus.SetNumEnemiesKilled(0);
         gameStatus.SetWaveNumber(1);
         gameStatus.SetTimeLeftInCurState(10.0f);

         GetGameManager()->AddActor(*(mServerGameStatusProxy.get()), false, true);
      }


      // Now we can go to the ready room.
      DoStateTransition(&Transition::TRANSITION_FORWARD);
   }

   /////////////////////////////////////////////////////////////////////////////
   bool GameLogicComponent::JoinNetwork(const std::string& role, int serverPort, const std::string& hostIP)
   {
      bool success = false;

      if (role == "Server" || role == "server" || role == "SERVER")
      {
         success = JoinNetworkAsServer(serverPort);
      }
      else if (role == "Client" || role == "client" || role == "CLIENT")
      {
         success = JoinNetworkAsClient(serverPort, hostIP);
      }

      return success;
   }

   /////////////////////////////////////////////////////////////////////////////
   bool GameLogicComponent::JoinNetworkAsServer(int serverPort)
   {
      bool result = false;

      dtNetGM::ServerNetworkComponent* serverComp;
      GetGameManager()->GetComponentByName(dtNetGM::ServerNetworkComponent::DEFAULT_NAME, serverComp);
      if( serverComp != NULL )
      {
         result = serverComp->SetupServer(serverPort);
         if (result)
         {         
            mIsConnectedToNetwork = true;
            mIsServer = true;
            mNetworkComp = serverComp;

            // Start a repeating timer - to update terrain
            GetGameManager()->SetTimer(TIMER_UPDATE_TERRAIN, NULL, 3.0f, true, true);
         }
      }
      else
      {
         LOG_ERROR("Critical Error - There is no Server Network Component by the default name.");
      }

      return result;
   }

   ///////////////////////////////////////////////////////////
   bool GameLogicComponent::JoinNetworkAsClient(int serverPort, const std::string &serverIPAddress)
   {
      bool result = false;
      mIsServer = false;

      dtNetGM::ClientNetworkComponent* clientComp;
      GetGameManager()->GetComponentByName(dtNetGM::ClientNetworkComponent::DEFAULT_NAME, clientComp);
      if( clientComp != NULL )
      {
         result = clientComp->SetupClient(serverIPAddress, serverPort);
         if (result)
         {
            mIsConnectedToNetwork = true;
            mNetworkComp = clientComp;
         }
         mIsServer = false;
      }

      return result;
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::DisconnectFromNetwork()
   {
      if (mIsConnectedToNetwork)
      {
         // SERVER
         if (mIsServer)
         {
            dtNetGM::ServerNetworkComponent *serverComponent = 
               dynamic_cast<dtNetGM::ServerNetworkComponent *>(mNetworkComp.get());
            if (serverComponent != NULL)
            {
               serverComponent->ShutdownNetwork();
            }

            GetGameManager()->ClearTimer(TIMER_UPDATE_TERRAIN, NULL); 
         }

         // CLIENT
         else 
         {
            dtNetGM::ClientNetworkComponent *clientComponent = 
               dynamic_cast<dtNetGM::ClientNetworkComponent *>(mNetworkComp.get());
            if (clientComponent != NULL)
            {
               clientComponent->ShutdownNetwork();
            }
         }

         mNetworkComp = NULL;
         mIsConnectedToNetwork = false;
      }
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandleStateChangeMessage( const SimCore::Components::GameStateChangedMessage& stateChange )
   {
      const SimCore::Components::StateType& state = stateChange.GetNewState();
      LOG_WARNING("Changing to stage[" + state.GetName() + "].");

      // Note - when we change the player status, it gets published when the actor is ticked.

      if (state == SimCore::Components::StateType::STATE_MENU)
      {
      }
      else if (state == SimCore::Components::StateType::STATE_LOADING)
      {
         if (mPlayerStatus != NULL)
            mPlayerStatus->SetPlayerStatus(PlayerStatusActor::PlayerStatusEnum::LOADING);
         SimCore::Utils::LoadMaps(*GetGameManager(), mMapName);
         // When loaded, we trap the MAP_LOADED message and finish our setup
      }
      else if (state == NetDemoState::STATE_UNLOAD)
      {
         if (mPlayerStatus != NULL)
            mPlayerStatus->SetPlayerStatus(PlayerStatusActor::PlayerStatusEnum::UNKNOWN);
         HandleUnloadingState();
      }
      else if (state == NetDemoState::STATE_GAME_RUNNING)
      {
         if (mPlayerStatus != NULL)
            mPlayerStatus->SetPlayerStatus(PlayerStatusActor::PlayerStatusEnum::IN_GAME_ALIVE);

         if (mStartTheGameOnNextGameRunning)
         {
            mServerGameStatusProxy->GetActorAsGameStatus().StartTheGame();            
            mStartTheGameOnNextGameRunning = false;
         }
      }
      else if (state == NetDemoState::STATE_GAME_READYROOM)
      {
         if (mPlayerStatus != NULL)
            mPlayerStatus->SetPlayerStatus(PlayerStatusActor::PlayerStatusEnum::IN_GAME_READYROOM);
         if (mServerGameStatusProxy != NULL)
            mServerGameStatusProxy->GetActorAsGameStatus().SetGameStatus
               (ServerGameStatusActor::ServerGameStatusEnum::READY_ROOM);
         mStartTheGameOnNextGameRunning = true;

         // curt - hack - replace this with the GUI COMPONENT
         DoStateTransition(&Transition::TRANSITION_FORWARD);
      }
      else if (state == SimCore::Components::StateType::STATE_SHUTDOWN )
      {
         GetGameManager()->GetApplication().Quit();
      }
   }


         /////////////////////////////////////////////////////////
         // Hack stuff - add a vehicle here. For testing purposes.
/*         HoverVehicleActorProxy* prototypeProxy = NULL;
         GetGameManager()->FindPrototypeByName("Hover Vehicle", prototypeProxy);
         if (prototypeProxy == NULL)
         {
            mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__,
               "Critical Error - can't find vehicle prototype [Hover Vehicle]. Likely error - incorrect additional maps in your config.xml. Compare to the config_example.xml.");
            return;
         }
         dtCore::RefPtr<HoverVehicleActorProxy> testVehicleProxy = NULL;
         GetGameManager()->CreateActorFromPrototype(prototypeProxy->GetId(), testVehicleProxy);
         if(testVehicleProxy != NULL)
         {
            //HoverVehicleActor *vehicleActor = dynamic_cast<HoverVehicleActor*>(testVehicleProxy->GetGameActor());
            //if (vehicleActor != NULL)
            //{
               GetGameManager()->AddActor(*testVehicleProxy.get(), false, true);

            //}
         }
         */

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::HandleUnloadingState()
   {
      // Have to disconnect first, else we will get network messages about stuff we don't understand.
      DisconnectFromNetwork();

      GetGameManager()->CloseCurrentMap(); 
      // When the map is unloaded, we get the UNLOADED msg and change our states back to lobby.
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::UnloadCurrentTerrain()
   {
      if (mCurrentTerrainDrawActor.valid())
      {
         // Delete the visible terrain
         GetGameManager()->DeleteActor(mCurrentTerrainDrawActor->GetGameActorProxy());
         mCurrentTerrainDrawActor = NULL;
         mCurrentTerrainPrototypeName = "";
         //mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__, "Now unloading the previous terrain.");
      }
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::LoadNewTerrain()
   {
      if (mCurrentTerrainPrototypeName.empty())
         return;

      // Find the prototype for the DRAWN terrain.
      SimCore::Actors::TerrainActorProxy* drawLandPrototypeProxy = NULL;
      GetGameManager()->FindPrototypeByName(mCurrentTerrainPrototypeName, drawLandPrototypeProxy);
      if (drawLandPrototypeProxy == NULL)
      {
         mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__,
            "Cannot load the drawLandPrototype [%s] from the GM. Likely error - incorrect additional maps in your config.xml. Compare to the config_example.xml.",
            mCurrentTerrainPrototypeName.c_str());
         mCurrentTerrainPrototypeName = "";
         return;
      }

      // Create a new instance of the DRAWN Terrain.
      dtCore::RefPtr<SimCore::Actors::TerrainActorProxy> newDrawLandActorProxy = NULL;
      GetGameManager()->CreateActorFromPrototype(drawLandPrototypeProxy->GetId(), newDrawLandActorProxy);
      if (!newDrawLandActorProxy.valid())
      {
         mLogger->LogMessage(dtUtil::Log::LOG_ALWAYS, __FUNCTION__, __LINE__,
            "Cannot create a newDrawLandActor for prototype [%s] from the GM. CRITICAL ERROR!",
            mCurrentTerrainPrototypeName.c_str());
         mCurrentTerrainPrototypeName = "";
         return;
      }


      // Add to the GM
      GetGameManager()->AddActor(*newDrawLandActorProxy, false, true);
      mCurrentTerrainDrawActor = dynamic_cast<SimCore::Actors::TerrainActor*>
         (newDrawLandActorProxy->GetActor());

   }

   //////////////////////////////////////////////////////////////////////////
   SimCore::Actors::TerrainActor* GameLogicComponent::GetCurrentTerrainDrawActor()
   {
      return dynamic_cast<SimCore::Actors::TerrainActor *>(mCurrentTerrainDrawActor.get());
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::SetIsServer(bool newValue)
   {
      if (!mPlayerStatus.valid()) mIsServer = newValue;
   }

   //////////////////////////////////////////////////////////////////////////
   bool GameLogicComponent::GetIsServer()
   {
      return mIsServer;
   }

   //////////////////////////////////////////////////////////////////////////
   void GameLogicComponent::SetMapName(const std::string& mapName)
   {
      mMapName = mapName;
   }

   //////////////////////////////////////////////////////////////////////////
   const std::string& GameLogicComponent::GetMapName() const
   {
      return mMapName;
   }

}