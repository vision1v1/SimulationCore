/* -*-c++-*-
 * SimulationCore
 * Copyright 2013, David Guthrie
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * David Guthrie
 */

#ifndef WEAPONINVENTORYACTCOMP_H_
#define WEAPONINVENTORYACTCOMP_H_

#include <dtUtil/getsetmacros.h>
#include <dtCore/resourcedescriptor.h>
#include <dtGame/actorcomponent.h>
// This is only needed for a constant for the muzzle point.
#include <SimCore/Actors/WeaponActor.h>

namespace SimCore
{
   namespace ActComps
   {
      class WeaponInventoryActComp : public dtGame::ActorComponent
      {
      public:
         static const dtGame::ActorComponent::ACType TYPE;

         typedef dtGame::ActorComponent BaseClass;

         class WeaponDescription : public dtCore::PropertyContainer
         {
         public:
            WeaponDescription();
            virtual ~WeaponDescription();
            DT_DECLARE_ACCESSOR(std::string, WeaponPrototypeName);
            DT_DECLARE_ACCESSOR(std::string, ShooterPrototypeName);
            DT_DECLARE_ACCESSOR(std::string, WeaponSwapRootNode);
            DT_DECLARE_ACCESSOR(std::string, WeaponHotSpotNode);
            DT_DECLARE_ACCESSOR(dtCore::ResourceDescriptor, FiringParticleSystem);
         };

         class WeaponData : public osg::Referenced
         {
         public:
            dtCore::RefPtr<SimCore::Actors::WeaponActorProxy> mWeapon;
            dtCore::RefPtr<WeaponDescription> mDescription;
            bool operator < (const WeaponData& toCompare) const;
         };

         WeaponInventoryActComp();

         ~WeaponInventoryActComp();

         void BuildPropertyMap();

         //////////////////////////////////////////////////////////////////////////
         void OnEnteredWorld();

         //////////////////////////////////////////////////////////////////////////
         void OnRemovedFromWorld();

         ////////////////////////////////////////////////////////////////////////////////
         WeaponInventoryActComp::WeaponData* CreateAndAddWeapon(WeaponDescription& wd, bool makeCurrent = false);

         void SelectNextWeapon();

         void SelectPreviousWeapon();

         void SelectWeapon( const std::string& weaponName );

         void SelectWeapon(WeaponData* wp);

         bool HasWeapon( const std::string& weaponName ) const;

         WeaponData* GetCurrentWeapon() const;
         const std::string& GetCurrentWeaponName() const;

         void RemoveWeapon( const std::string& weaponName );

         WeaponData* FindWeapon( const std::string& weaponName );

         const WeaponData* FindWeapon( const std::string& weaponName ) const;

         int FindWeaponIndex(const std::string& weaponName) const;

         int FindWeaponIndex( const WeaponData* wp ) const;

         unsigned GetNumWeapons() const;

         void StartFiring();

         void StopFiring();

         DT_DECLARE_ARRAY_ACCESSOR(dtCore::RefPtr<WeaponDescription>, WeaponDescription, WeaponDescriptions);

      private:

         void CreateWeaponsFromDescriptions();
         void CreateWeapon(dtCore::RefPtr<WeaponDescription>& wd);

         dtCore::RefPtr<WeaponData> mCurrentWeapon;
         typedef std::vector<dtCore::RefPtr<WeaponData> > WeaponVector;
         WeaponVector mWeapons;
      };

   }
}


#endif /* WEAPONINVENTORYACTCOMP_H_ */
