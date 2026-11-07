/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// DeployStyleAIUpdate.cpp ////////////
// Author: Kris Morness, August 2002
// Desc:   A standard ai update that also handles units that must deploy to attack and pack before moving.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"

#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
DeployStyleAIUpdate::DeployStyleAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_state = READY_TO_MOVE;
	m_frameToWaitForDeploy = 0;
	m_manualLockedDeploy = FALSE;
	m_hasManualDeployPos = FALSE;
	m_pendingManualUndeploy = FALSE;
	m_pendingManualDeployLock = FALSE;
	m_hasSavedRanges = FALSE;
	m_originalVisionRange = 0.0f;
	m_originalShroudClearingRange = 0.0f;
}

//-------------------------------------------------------------------------------------------------
DeployStyleAIUpdate::~DeployStyleAIUpdate()
{
}

static void fireDeployTransitionWeapon(Object* object, const WeaponTemplate* weaponTemplate)
{
	if (weaponTemplate == nullptr)
		return;

	Coord3D targetPosition = *object->getPosition();
	AIUpdateInterface* ai = object->getAIUpdateInterface();
	if (ai != nullptr)
	{
		Object* target = ai->getCurrentVictim();
		if (target != nullptr)
		{
			targetPosition = *target->getPosition();
		}
		else
		{
			const Coord3D* currentVictimPosition = ai->getCurrentVictimPos();
			if (currentVictimPosition != nullptr)
				targetPosition = *currentVictimPosition;
		}
	}

	Weapon* weapon = TheWeaponStore->allocateNewWeapon(weaponTemplate, PRIMARY_WEAPON);
	weapon->loadAmmoNow(object);
	weapon->forceFireWeapon(object, &targetPosition);
	deleteInstance(weapon);
}

//-------------------------------------------------------------------------------------------------
Bool DeployStyleAIUpdate::isIdle() const
{
	return AIUpdateInterface::isIdle();
}

void DeployStyleAIUpdate::setManualLockedDeploy(Bool locked, const Coord3D* deployPos)
{
	AIUpdateInterface* ai = getObject()->getAI();

	if (locked)
	{
		if (deployPos != nullptr)
		{
			m_manualDeployPos = *deployPos;
			m_hasManualDeployPos = TRUE;

			if (ai != nullptr)
			{
				m_manualLockedDeploy = FALSE;

				ai->aiMoveToPosition(&m_manualDeployPos, CMD_FROM_PLAYER);

				m_pendingManualDeployLock = TRUE;

				const ThingTemplate* templ = getObject()->getTemplate();
				if (templ)
				{
					const AudioEventRTS* v = templ->getVoiceDeploy();
					if (v && !v->getEventName().isEmpty())
					{
						AudioEventRTS voice = *v;
						voice.setObjectID(getObject()->getID());
						TheAudio->addAudioEvent(&voice);
					}
				}
			}
			else
			{
				m_manualLockedDeploy = TRUE;
			}
		}
		else
		{
			m_hasManualDeployPos = FALSE;

			if (ai != nullptr)
			{
				AICommandParms stopCmd(AICMD_IDLE, CMD_FROM_PLAYER);
				ai->aiDoCommand(&stopCmd);
			}

			m_manualLockedDeploy = TRUE;

			{
				const ThingTemplate* templ = getObject()->getTemplate();
				if (templ)
				{
					const AudioEventRTS* v = templ->getVoiceDeploy();
					if (v && !v->getEventName().isEmpty())
					{
						AudioEventRTS voice = *v;
						voice.setObjectID(getObject()->getID());
						TheAudio->addAudioEvent(&voice);
					}
				}
			}

			switch (m_state)
			{
			case READY_TO_ATTACK:
				break;

			case UNDEPLOY:
				if (m_frameToWaitForDeploy != 0)
				{
					setMyState(DEPLOY, TRUE);
				}
				else
				{
					setMyState(DEPLOY);
				}
				break;

			case READY_TO_MOVE:
				setMyState(DEPLOY);
				break;

			case DEPLOY:
				break;

			case ALIGNING_TURRETS:
				break;
			}
		}
	}
	else
	{
		m_manualLockedDeploy = FALSE;
		m_hasManualDeployPos = FALSE;

		{
			const ThingTemplate* templ = getObject()->getTemplate();
			if (templ)
			{
				const AudioEventRTS* v = templ->getVoiceUnDeploy();
				if (v && !v->getEventName().isEmpty())
				{
					AudioEventRTS voice = *v;
					voice.setObjectID(getObject()->getID());
					TheAudio->addAudioEvent(&voice);
				}
			}
		}

		if (m_state == DEPLOY)
		{
			if (m_frameToWaitForDeploy != 0)
			{
				setMyState(UNDEPLOY, TRUE);
			}
			else
			{
				setMyState(UNDEPLOY);
			}
		}
		else if (m_state == READY_TO_ATTACK)
		{
			WhichTurretType tur = getWhichTurretForCurWeapon();

			if (tur != TURRET_INVALID &&
				doTurretsHaveToCenterBeforePacking())
			{
				m_pendingManualUndeploy = TRUE;
				setMyState(ALIGNING_TURRETS);
			}
			else
			{
				setMyState(UNDEPLOY);
			}
		}

		if (ai != nullptr)
		{
			ai->setCurrentVictim(nullptr);
			AICommandParms stopCmd(AICMD_IDLE, CMD_FROM_PLAYER);
			ai->aiDoCommand(&stopCmd);
		}
	}
}

Bool DeployStyleAIUpdate::isManualLockedDeploy() const
{
	// TheSuperHackers @bugfix altnt5500-pixel 07/11/2026 Allow commands until manual redeployment completes.
	return m_manualLockedDeploy && m_state == READY_TO_ATTACK;
}

static Bool isAttackCommandWithinWeaponRange(Object* object, const AICommandParms* parms)
{
	if (object == nullptr || parms == nullptr)
		return FALSE;

	Weapon* weapon = object->getCurrentWeapon();
	if (weapon == nullptr)
		return FALSE;

	switch (parms->m_cmd)
	{
	case AICMD_ATTACK_POSITION:
		return weapon->isWithinAttackRange(object, &parms->m_pos);

	case AICMD_ATTACK_OBJECT:
	case AICMD_FORCE_ATTACK_OBJECT:
		return parms->m_obj != nullptr && weapon->isWithinAttackRange(object, parms->m_obj);

	default:
		return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::aiDoCommand( const AICommandParms* parms )
{
	//if (!isAllowedToRespondToAiCommands(parms))
	//	return;
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	if (parms &&
		parms->m_cmdSource == CMD_FROM_PLAYER &&
		m_pendingManualDeployLock && !m_manualLockedDeploy)
	{
		m_manualLockedDeploy = FALSE;
		m_hasManualDeployPos = FALSE;
		m_pendingManualUndeploy = FALSE;
		m_pendingManualDeployLock = FALSE;
	}

	const ThingTemplate* objectTemplate = getObject()->getTemplate();
	if (parms &&
		parms->m_cmdSource == CMD_FROM_PLAYER &&
		!m_manualLockedDeploy &&
		objectTemplate != nullptr &&
		objectTemplate->preferManualDeployLogic() &&
		(parms->m_cmd == AICMD_ATTACK_POSITION ||
			parms->m_cmd == AICMD_ATTACK_OBJECT ||
			parms->m_cmd == AICMD_FORCE_ATTACK_OBJECT))
	{
		if (isAttackCommandWithinWeaponRange(getObject(), parms))
		{
			setManualLockedDeploy(TRUE, nullptr);
		}
		else
		{
			m_hasManualDeployPos = FALSE;
			m_pendingManualDeployLock = TRUE;
		}
	}

	if (isManualLockedDeploy() &&
		parms &&
		parms->m_cmdSource == CMD_FROM_PLAYER)
	{
		if (parms->m_cmd == AICMD_IDLE)
		{
			if (m_state != READY_TO_ATTACK)
				return;

			privateIdle(parms->m_cmdSource);
			setCurrentVictim(nullptr);
			return;
		}

		if (parms->m_cmd != AICMD_ATTACK_POSITION &&
			parms->m_cmd != AICMD_ATTACK_OBJECT &&
			parms->m_cmd != AICMD_FORCE_ATTACK_OBJECT)
			return;

		if (m_state == READY_TO_ATTACK && !isAttackCommandWithinWeaponRange(getObject(), parms))
			return;
	}

	/*
	//Hack code to allow follow waypoint scripts to be converted to attack follow waypoint scripts
	//for simple-test-map-purposes.
	switch( parms->m_cmd )
	{
		case AICMD_FOLLOW_WAYPOINT_PATH:
			((AICommandParms*)parms)->m_cmd = AICMD_ATTACKFOLLOW_WAYPOINT_PATH;
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			((AICommandParms*)parms)->m_cmd = AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM;
			break;
	}
	*/
	AIUpdateInterface::aiDoCommand( parms );
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime DeployStyleAIUpdate::update()
{
	// have to call our parent's isIdle, because we override it to never return true
	// when we have a pending command...
	Object *self = getObject();
	Weapon *weapon = self->getCurrentWeapon();
	UnsignedInt now = TheGameLogic->getFrame();
	const DeployStyleAIUpdateModuleData *data = getDeployStyleAIUpdateModuleData();

	//Are we attempting to move? If so we can't do it unless we are undeployed.
	Bool isTryingToMove = isWaitingForPath() || getPath();

	//Are we trying to attack something. If so, we need to be in range before we can do so.
	Bool isTryingToAttack = getStateMachine()->isInAttackState();
	Bool isInRange = FALSE;

	//Are we in guard mode? If so, are we idle... idle guarders deploy for fastest response against attackers.
	Bool isInGuardIdleState = getStateMachine()->isInGuardIdleState();

	AIUpdateInterface *ai = self->getAI();

	if (m_state == ALIGNING_TURRETS && m_pendingManualUndeploy)
	{
		WhichTurretType tur = getWhichTurretForCurWeapon();

		if (tur == TURRET_INVALID || isTurretInNaturalPosition(tur))
		{
			m_pendingManualUndeploy = FALSE;
			setMyState(UNDEPLOY);
		}
	}

	if( isTryingToAttack && weapon )
	{
		Object *victim = ai->getCurrentVictim();
		if( victim )
		{
			isInRange = weapon->isWithinAttackRange( self, victim );
		}
		else
		{
			const Coord3D *pos = ai->getCurrentVictimPos();
			if( pos )
			{
				isInRange = weapon->isWithinAttackRange( self, pos );
			}
		}
	}

	if( m_frameToWaitForDeploy != 0 && now >= m_frameToWaitForDeploy)
	{
		switch( m_state )
		{
			case DEPLOY:
				setMyState( READY_TO_ATTACK );
				break;
			case UNDEPLOY:
				setMyState( READY_TO_MOVE );
				break;
		}
	}

	if (m_state == READY_TO_ATTACK)
	{
		if (ai &&
				ai->getCurrentVictim() == nullptr &&
				!isTryingToAttack)
		{
			if (m_manualLockedDeploy || m_hasManualDeployPos)
			{
				{
					Object* newVictim = getNextMoodTarget(TRUE, FALSE);
					if (newVictim)
					{
						if (weapon == nullptr || weapon->isWithinAttackRange(self, newVictim))
						{
							privateAttackObject(newVictim, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
						}
						else
						{
						}
					}
				}
			}
		}
	}

#if !RETAIL_COMPATIBLE_CRC
	if (m_state == DEPLOY && isTryingToAttack && !isInRange)
	{
		if (m_frameToWaitForDeploy != 0)
		{
			setMyState(UNDEPLOY, TRUE);
		}
		else
		{
			if (!m_manualLockedDeploy)
			{
				setMyState(UNDEPLOY);
			}
		}
	}
#endif

	Bool isReadyForPendingManualDeploy =
		(m_state == READY_TO_MOVE && (m_hasManualDeployPos || (isTryingToAttack && isInRange))) ||
		(m_state == READY_TO_ATTACK && isTryingToAttack && isInRange);
	if (m_pendingManualDeployLock &&
		isReadyForPendingManualDeploy &&
		!ai->isMoving())
	{
		m_pendingManualDeployLock = FALSE;
		m_manualLockedDeploy = TRUE;

		if (m_state == READY_TO_MOVE)
			setMyState(DEPLOY);
		m_hasManualDeployPos = FALSE;
	}

	//if( isInRange || isInGuardIdleState )
	if ((isInRange && !ai->isMoving()) || isInGuardIdleState)
	{
		switch( m_state )
		{
			case READY_TO_MOVE:
				//We're need to deploy before we can attack.
				setMyState( DEPLOY );
				break;
			case READY_TO_ATTACK:
				//Let the AI handle attacking.
				break;
			case DEPLOY:
				//We can't start attacking until we are fully deployed.
				break;
			case UNDEPLOY:
				if( m_frameToWaitForDeploy != 0 )
				{
					//Reverse the undeploy at it's current frame!
					setMyState( DEPLOY, TRUE );
				}
				break;
			case ALIGNING_TURRETS:
				//If turrets are aligning, we are able to attack right now.
				setMyState( READY_TO_ATTACK );
				break;
		}
	}
	else if( isTryingToMove )
	{
		switch( m_state )
		{
			case READY_TO_MOVE:
				//We're ready... ai will handle moving.
				break;
			case READY_TO_ATTACK:
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					if( doTurretsHaveToCenterBeforePacking() )
					{
						//Turrets need to center before we can undeploy.
						setMyState( ALIGNING_TURRETS );
						break;
					}
				}
				//Start undeploying.
				setMyState( UNDEPLOY );
				break;
			}
			case DEPLOY:
				if( m_frameToWaitForDeploy != 0 )
				{
					//Reverse the deploy at it's current frame!
					setMyState( UNDEPLOY, TRUE );
				}
				break;
			case UNDEPLOY:
				break;
			case ALIGNING_TURRETS:
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					if( isTurretInNaturalPosition( tur ) )
					{
						//Turrets are centers, so now we can undeploy.
						setMyState( UNDEPLOY );
					}
				}
				break;
			}
		}
	}

	switch( m_state )
	{
		case READY_TO_MOVE:
			if( isTryingToMove )
			{
				self->setModelConditionState( MODELCONDITION_MOVING );
			}
			break;

		case READY_TO_ATTACK:
			break;

		case DEPLOY:
			if( data->m_manualDeployAnimations )
			{
				UnsignedInt totalFrames = getPackTime();
				UnsignedInt framesLeft = m_frameToWaitForDeploy - now;
				Drawable *draw = self->getDrawable();
				if( draw )
				{
					draw->setAnimationFrame( totalFrames - framesLeft );
				}
			}
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;
		case UNDEPLOY:
			if( data->m_manualDeployAnimations )
			{
				UnsignedInt framesLeft = m_frameToWaitForDeploy - now;
				Drawable *draw = self->getDrawable();
				if( draw )
				{
					draw->setAnimationFrame( framesLeft );
				}
			}
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;

		case ALIGNING_TURRETS:
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;
	}

	AIUpdateInterface::update();
	//We can't sleep the deploy AI because any new commands need to be caught and sent
	//into busy state during the update.
	return UPDATE_SLEEP_NONE;

}

//-------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::setMyState( DeployStateTypes stateID, Bool reverseDeploy )
{
	const DeployStateTypes previousState = m_state;
	Object *self = getObject();
	const ThingTemplate *thing = self->getTemplate();
	if (stateID == DEPLOY && !m_hasSavedRanges &&
		(thing->getDeployedVisionRange() >= 0.0f || thing->getDeployedShroudClearingRange() >= 0.0f))
	{
		m_originalVisionRange = self->getVisionRange();
		m_originalShroudClearingRange = self->getShroudClearingRange();
		m_hasSavedRanges = TRUE;
	}

	m_state = stateID;
	UnsignedInt now = TheGameLogic->getFrame();
	const DeployStyleAIUpdateModuleData *data = getDeployStyleAIUpdateModuleData();

	switch( stateID )
	{
		case DEPLOY:
		{
			//Tell our object to deploy (so it can continue the same attack later).
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ) );

			if( reverseDeploy )
			{
				//This is a zero to max animation.
				UnsignedInt totalFrames = getUnpackTime();
				UnsignedInt framesLeft = m_frameToWaitForDeploy - now;
				m_frameToWaitForDeploy = now + totalFrames - framesLeft;
				if( data->m_manualDeployAnimations )
				{
					Drawable *draw = self->getDrawable();
					if( draw )
					{
						draw->setAnimationFrame( totalFrames - framesLeft );
					}
				}
			}
			else
			{
				m_frameToWaitForDeploy = getUnpackTime() + now;
			}

			//Play deploy sound
			const AudioEventRTS* soundToPlayPtr = thing->getPerUnitSound( "Deploy" );
			if( soundToPlayPtr )
			{
				AudioEventRTS soundToPlay = *soundToPlayPtr;
				soundToPlay.setObjectID( self->getID() );
				TheAudio->addAudioEvent( &soundToPlay );
			}

			break;
		}
		case UNDEPLOY:
		{
			if (m_hasSavedRanges)
			{
				if (thing->getDeployedVisionRange() >= 0.0f)
					self->setVisionRange(m_originalVisionRange);
				if (thing->getDeployedShroudClearingRange() >= 0.0f)
					self->setShroudClearingRange(m_originalShroudClearingRange);
			}

			//This status will tell the approach AI state to succeed automatically. This prevents
			//twitching on deploy. Make sure we clear it now so when it goes into that state, it'll
			//actually move.
			self->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );

			//Tell our object to pack up (so it can continue the same move later).
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2( MODELCONDITION_UNPACKING, MODELCONDITION_DEPLOYED ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );

			if( reverseDeploy )
			{
				//This is a max to zero animation.
				UnsignedInt totalFrames = getUnpackTime();
				UnsignedInt framesLeft = m_frameToWaitForDeploy - now;
				m_frameToWaitForDeploy = now + totalFrames - framesLeft;
				if( data->m_manualDeployAnimations )
				{
					Drawable *draw = self->getDrawable();
					if( draw )
					{
						draw->setAnimationFrame( framesLeft );
					}
				}
			}
			else
			{
				m_frameToWaitForDeploy = getPackTime() + now;
			}

			if( doTurretsFunctionOnlyWhenDeployed() )
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					setTurretEnabled( tur, false );
				}
			}

			//Play undeploy sound
			const AudioEventRTS* soundToPlayPtr = thing->getPerUnitSound( "Undeploy" );
			if( soundToPlayPtr )
			{
				AudioEventRTS soundToPlay = *soundToPlayPtr;
				soundToPlay.setObjectID( self->getID() );
				TheAudio->addAudioEvent( &soundToPlay );
			}

			break;
		}
		case READY_TO_MOVE:
		{
			m_frameToWaitForDeploy = 0;
			m_hasSavedRanges = FALSE;

			self->clearModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );
			break;
		}
		case READY_TO_ATTACK:
		{
			if (m_hasSavedRanges)
			{
				if (thing->getDeployedVisionRange() >= 0.0f)
					self->setVisionRange(thing->getDeployedVisionRange());
				if (thing->getDeployedShroudClearingRange() >= 0.0f)
					self->setShroudClearingRange(thing->getDeployedShroudClearingRange());
			}

			//This status will tell the approach AI state to succeed automatically. This prevents
			//twitching on deploy.
			self->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );

			m_frameToWaitForDeploy = 0;

			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_DEPLOYED) );

			if( doTurretsFunctionOnlyWhenDeployed() )
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					setTurretEnabled( tur, true );
				}
			}

			break;
		}
		case ALIGNING_TURRETS:
		{
			m_frameToWaitForDeploy = 0;
			WhichTurretType tur = getWhichTurretForCurWeapon();
			if( tur != TURRET_INVALID )
			{
				recenterTurret( tur );
			}
			break;
		}
	}

	if (previousState == DEPLOY && stateID == READY_TO_ATTACK)
		fireDeployTransitionWeapon(self, thing->getFireWeaponUponDeployed());
	else if (previousState == UNDEPLOY && stateID == READY_TO_MOVE)
		fireDeployTransitionWeapon(self, thing->getFireWeaponUponUndeploying());

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added support for attack move
	* 3: Added improved support for guard, and support for hunt AI
	* 5: Added saved original vision and shroud-clearing ranges
 **/
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::xfer( Xfer *xfer )
{
  // version
	XferVersion currentVersion = 5;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);

	if( version >= 4 )
	{
		xfer->xferUser(&m_state, sizeof(m_state));
		xfer->xferUnsignedInt(&m_frameToWaitForDeploy);
		if (version >= 5)
		{
			xfer->xferBool(&m_hasSavedRanges);
			xfer->xferReal(&m_originalVisionRange);
			xfer->xferReal(&m_originalShroudClearingRange);
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			m_hasSavedRanges = FALSE;
			m_originalVisionRange = 0.0f;
			m_originalShroudClearingRange = 0.0f;
		}
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		//Good riddance!!!
		Bool obsoleteBool;
		UnsignedInt obsoleteUnsignedInt;
		ObjectID obsoleteObjectID;
		Coord3D obsoleteCoord3D;
   	AICommandParmsStorage	obsoleteAICommandParmsStorage;

		xfer->xferBool( &obsoleteBool );
		xfer->xferUnsignedInt( &obsoleteUnsignedInt );
		xfer->xferUser(&m_state, sizeof(m_state));

		if( version >= 2 )
		{
			xfer->xferObjectID( &obsoleteObjectID );
			xfer->xferObjectID( &obsoleteObjectID );
			xfer->xferCoord3D( &obsoleteCoord3D );
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
		}
		if( version >= 3 )
		{
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
		}

		obsoleteAICommandParmsStorage.doXfer( xfer );

		if( version < 2 )
		{
			obsoleteAICommandParmsStorage.doXfer(xfer);
		}

		//Initialize unit to able to move.
		m_state = READY_TO_MOVE;
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::loadPostProcess()
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}

