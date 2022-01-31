/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"

#define TF_DEFS_ONLY
#include "tf_defs.h"

#include "usercmd.h"
#include "entity_state.h"
#include "demo_api.h"
#include "pm_defs.h"
#include "event_api.h"
#include "r_efx.h"

#include "../hud_iface.h"
#include "../com_weapons.h"
#include "../demo.h"

#include "entity_types.h"

#include "bench.h"
#include "com_model.h"

extern globalvars_t *gpGlobals;
extern int g_iUser1;

// Pool of client side entities/entvars_t
static entvars_t ev[32];
static int num_ents = 0;

// The entity we'll use to represent the local client
static CBasePlayer player;

// Local version of game .dll global variables ( time, etc. )
static globalvars_t Globals;

static CBasePlayerWeapon *g_pWpns[32];

float g_flApplyVel = 0.0;

struct laserdot_info_t
{
	Vector previousorigin;
	int laserdotactive;
	int laserdotintensity;
};

static laserdot_info_t g_laserdot;

CTFShotgun g_Gun;
CTFSuperShotgun g_Super;
CTFNailgun g_NG;
CTFSuperNailgun g_SNG;
CTFTranq g_Tranq;
CTFFlamethrower g_Flame;
CTFIncendiaryC g_IC;
CTFRailgun g_Rail;
CTFRpg g_RPG;
CTFAutoRifle g_AR;
CTFAssaultC g_AC;
CTFGrenadeLauncher g_GL;
CTFPipebombLauncher g_PL;
CTFSniperRifle g_Sniper;
CTFKnife g_Knife;
CTFSpanner g_Spanner;
CTFMedikit g_Medkit;
CTFAxe g_Crowbar;
CLaserSpot g_Spot;

/*
======================
AlertMessage

Print debug messages to console
======================
*/
void AlertMessage( ALERT_TYPE atype, const char *szFmt, ... )
{
	va_list argptr;
	static char string[1024];

	va_start( argptr, szFmt );
	vsprintf( string, szFmt, argptr );
	va_end( argptr );

	gEngfuncs.Con_Printf( "cl:  " );
	gEngfuncs.Con_Printf( string );
}

//Returns if it's multiplayer.
//Mostly used by the client side weapons.
bool bIsMultiplayer( void )
{
	return gEngfuncs.GetMaxClients() == 1 ? 0 : 1;
}

//Just loads a v_ model.
void LoadVModel( const char *szViewModel, CBasePlayer *m_pPlayer )
{
	gEngfuncs.CL_LoadModel( szViewModel, &m_pPlayer->pev->viewmodel );
}

/*
=====================
HUD_PrepEntity

Links the raw entity to an entvars_s holder.  If a player is passed in as the owner, then
we set up the m_pPlayer field.
=====================
*/
void HUD_PrepEntity( CBaseEntity *pEntity, CBasePlayer *pWeaponOwner, int ammotype )
{
	memset( &ev[num_ents], 0, sizeof( entvars_t ) );
	pEntity->pev = &ev[num_ents++];

	pEntity->Precache();
	pEntity->Spawn();

	if ( pWeaponOwner )
	{
		ItemInfo info;

		( (CBasePlayerWeapon *)pEntity )->m_pPlayer = pWeaponOwner;

		switch ( ammotype )
		{
		case 1:
			( (CBasePlayerWeapon *)pEntity )->current_ammo = &pWeaponOwner->ammo_shells;
			break;
		case 2:
			( (CBasePlayerWeapon *)pEntity )->current_ammo = &pWeaponOwner->ammo_cells;
			break;
		case 3:
			( (CBasePlayerWeapon *)pEntity )->current_ammo = &pWeaponOwner->ammo_nails;
			break;
		case 4:
			( (CBasePlayerWeapon *)pEntity )->current_ammo = &pWeaponOwner->ammo_rockets;
			break;
		}

		( (CBasePlayerWeapon *)pEntity )->GetItemInfo( &info );

		g_pWpns[info.iId] = (CBasePlayerWeapon *)pEntity;
	}
}

/*
=====================
CBaseEntity::Killed

If weapons code "kills" an entity, just set its effects to EF_NODRAW
=====================
*/
void CBaseEntity::Killed( entvars_t *pevInflictor, entvars_t *pevAttacker, int iGib )
{
	pev->effects |= EF_NODRAW;
}

/*
=====================
CBasePlayerWeapon::DefaultReload
=====================
*/
BOOL CBasePlayerWeapon::DefaultReload( int iClipSize, int iAnim, float fDelay, int body )
{
	if ( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return FALSE;

	int j = Q_min( iClipSize - m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] );

	if ( j == 0 )
		return FALSE;

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + fDelay;

	//!!UNDONE -- reload sound goes here !!!
	SendWeaponAnim( iAnim, UseDecrement(), body );

	m_fInReload = TRUE;

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 3.0f;
	return TRUE;
}

/*
=====================
CBasePlayerWeapon::CanDeploy
=====================
*/
BOOL CBasePlayerWeapon::CanDeploy( void )
{
	int bHasAmmo;
	ItemInfo info;

	GetItemInfo( &info );

	if ( info.pszAmmo1 && info.iAmmo1 != -1 && this != &g_Sniper )
	{
		if ( current_ammo )
			bHasAmmo = *current_ammo;
		else
			bHasAmmo = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;

		if ( info.pszAmmo2 )
			bHasAmmo |= m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType] != 0;

		if ( m_iClip <= 0 )
			return bHasAmmo;
	}

	return true;
}

/*
=====================
CBasePlayerWeapon::DefaultDeploy

=====================
*/
BOOL CBasePlayerWeapon::DefaultDeploy( const char *szViewModel, const char *szWeaponModel, int iAnim, const char *szAnimExt, int skiplocal, int body )
{
	if ( !CanDeploy() )
		return FALSE;

	gEngfuncs.CL_LoadModel( szViewModel, &m_pPlayer->pev->viewmodel );
	m_pPlayer->current_weapon = m_iId;
	SendWeaponAnim( iAnim, skiplocal, body );
	m_pPlayer->m_flNextAttack = 0.5f;
	m_flTimeWeaponIdle = 1.0f;
	return TRUE;
}

/*
=====================
CBasePlayerWeapon::PlayEmptySound

=====================
*/
BOOL CBasePlayerWeapon::PlayEmptySound( void )
{
	if ( m_iPlayEmptySound )
	{
		HUD_PlaySound( "weapons/357_cock1.wav", 0.8f );
		m_iPlayEmptySound = 0;
		return 0;
	}
	return 0;
}

/*
=====================
CBasePlayerWeapon::ResetEmptySound

=====================
*/
void CBasePlayerWeapon::ResetEmptySound( void )
{
	m_iPlayEmptySound = 1;
}

/*
=====================
CBasePlayerWeapon::Holster

Put away weapon
=====================
*/
void CBasePlayerWeapon::Holster( int skiplocal /* = 0 */ )
{
	m_pPlayer->tfstate &= ~TFSTATE_RELOADING;
	m_fInReload = false;
	m_pPlayer->pev->viewmodel = 0;
}

/*
=====================
CBasePlayerWeapon::SendWeaponAnim

Animate weapon model
=====================
*/
void CBasePlayerWeapon::SendWeaponAnim( int iAnim, int skiplocal, int body )
{
	m_pPlayer->pev->weaponanim = iAnim;
	HUD_SendWeaponAnim( iAnim, ( this != &g_Tranq ) + 1, 0 );
}

CLaserSpot *CLaserSpot::CreateSpot( void )
{
	g_Spot.pev->effects &= ~EF_NODRAW;
	return &g_Spot;
}

#if 0
/*
=====================
CBaseEntity::FireBulletsPlayer

Only produces random numbers to match the server ones.
=====================
*/
Vector CBaseEntity::FireBulletsPlayer( ULONG cShots, Vector vecSrc, Vector vecDirShooting, Vector vecSpread, float flDistance, int iBulletType, int iTracerFreq, int iDamage, entvars_t *pevAttacker, int shared_rand )
{
	float x = 0.0f, y = 0.0f, z;

	for ( ULONG iShot = 1; iShot <= cShots; iShot++ )
	{
		if ( pevAttacker == NULL )
		{
			// get circular gaussian spread
			do
			{
				x = RANDOM_FLOAT( -0.5f, 0.5f ) + RANDOM_FLOAT( -0.5f, 0.5f );
				y = RANDOM_FLOAT( -0.5f, 0.5f ) + RANDOM_FLOAT( -0.5f, 0.5f );
				z = x * x + y * y;
			} while ( z > 1 );
		}
		else
		{
			//Use player's random seed.
			// get circular gaussian spread
			x = UTIL_SharedRandomFloat( shared_rand + iShot, -0.5f, 0.5f ) + UTIL_SharedRandomFloat( shared_rand + ( 1 + iShot ), -0.5f, 0.5f );
			y = UTIL_SharedRandomFloat( shared_rand + ( 2 + iShot ), -0.5f, 0.5f ) + UTIL_SharedRandomFloat( shared_rand + ( 3 + iShot ), -0.5f, 0.5f );
			// z = x * x + y * y;
		}
	}

	return Vector( x * vecSpread.x, y * vecSpread.y, 0.0f );
}
#endif

/*
=====================
CBasePlayerWeapon::ItemPostFrame

Handles weapon firing, reloading, etc.
=====================
*/
void CBasePlayerWeapon::ItemPostFrame( void )
{
	int *ammo;
	ItemInfo info;

	GetItemInfo( &info );
	ammo = current_ammo ? current_ammo : &m_pPlayer->m_rgAmmo[this->m_iPrimaryAmmoType];

	if ( ( m_fInReload ) && ( m_pPlayer->m_flNextAttack <= 0.0f ) )
	{
		if ( *ammo <= info.iMaxClip - m_iClip )
		{
			m_iClip += *ammo;
			*ammo -= ( info.iMaxClip - m_iClip );
		}
		else
		{
			m_iClip = info.iMaxClip;
		}

		m_fInReload = 0;
		m_pPlayer->tfstate &= ~TFSTATE_RELOADING;
	}

	if ( ( m_pPlayer->pev->button & IN_ATTACK2 ) && ( m_flNextSecondaryAttack <= 0.0f ) )
	{
		if ( info.pszAmmo2 && !m_pPlayer->m_rgAmmo[SecondaryAmmoIndex()] )
		{
			m_fFireOnEmpty = TRUE;
		}

		SecondaryAttack();
		m_pPlayer->pev->button &= ~IN_ATTACK2;
	}
	else if ( ( m_pPlayer->pev->button & IN_ATTACK ) && ( m_flNextPrimaryAttack <= 0.0f ) )
	{
		if ( ( !m_iClip && info.pszAmmo1 ) || ( info.iMaxClip == WEAPON_NOCLIP && !m_pPlayer->m_rgAmmo[PrimaryAmmoIndex()] ) )
		{
			m_fFireOnEmpty = TRUE;
		}

		if ( m_pPlayer->super_damage_finished > gpGlobals->time )
		{
			if ( gpGlobals->time > m_pPlayer->m_fSuperSound )
			{
				m_fSuperSound = gpGlobals->time + 1.0f;
			}
		}

		PrimaryAttack();
	}
	else if ( m_pPlayer->pev->button & IN_RELOAD && info.iMaxClip != WEAPON_NOCLIP && !m_fInReload )
	{
		Reload();
	}
	else if ( !( m_pPlayer->pev->button & ( IN_ATTACK | IN_ATTACK2 ) ) )
	{
		m_fFireOnEmpty = FALSE;

		if ( !m_iClip && !( info.iFlags & ITEM_FLAG_NOAUTORELOAD ) && m_flNextPrimaryAttack < 0.0f )
		{
			Reload();
			return;
		}

		WeaponIdle();
		return;
	}

	if ( ShouldWeaponIdle() )
	{
		WeaponIdle();
	}
}

/*
=====================
CBasePlayer::SelectItem

  Switch weapons
=====================
*/
void CBasePlayer::SelectItem( const char *pstr )
{
	if ( !pstr )
		return;

	CBasePlayerItem *pItem = NULL;

	if ( !pItem )
		return;

	if ( pItem == m_pActiveItem )
		return;

	if ( m_pActiveItem )
		m_pActiveItem->Holster();

	m_pLastItem = m_pActiveItem;
	m_pActiveItem = pItem;

	if ( m_pActiveItem )
	{
		m_pActiveItem->Deploy();
	}
}

/*
=====================
CBasePlayer::SelectLastItem

=====================
*/
void CBasePlayer::SelectLastItem( void )
{
	if ( !m_pLastItem )
	{
		return;
	}

	if ( m_pActiveItem && !m_pActiveItem->CanHolster() )
	{
		return;
	}

	if ( m_pActiveItem )
		m_pActiveItem->Holster();

	CBasePlayerItem *pTemp = m_pActiveItem;
	m_pActiveItem = m_pLastItem;
	m_pLastItem = pTemp;
	m_pActiveItem->Deploy();
}

/*
=====================
CBasePlayer::Killed

=====================
*/
void CBasePlayer::Killed( entvars_t *pevInflictor, entvars_t *pevAttacker, int iGib )
{
	// Holster weapon immediately, to allow it to cleanup
	if ( m_pActiveItem )
		m_pActiveItem->Holster();

	tfstate &= ~TFSTATE_AIMING;
	TeamFortress_SetSpeed();
	gEngfuncs.pEventAPI->EV_StopSound( gEngfuncs.GetLocalPlayer()->index, CHAN_STATIC, "weapons/timer.wav" );
}

/*
=====================
CBasePlayer::Spawn

=====================
*/
void CBasePlayer::Spawn( void )
{
	if ( m_pActiveItem )
		m_pActiveItem->Deploy();
}

void CBasePlayer::TeamFortress_SetSpeed( void )
{
	if ( tfstate & TFSTATE_CANT_MOVE || !pev->playerclass )
	{
		pev->velocity = g_vecZero;
		g_engfuncs.pfnSetClientMaxspeed( ENT( pev ), 1.0f );
		pev->maxspeed = 1.0f;
	}
	else
	{
		switch ( pev->playerclass )
		{
		case PC_SCOUT: pev->maxspeed = PC_SCOUT_MAXSPEED; break;
		case PC_SNIPER: pev->maxspeed = PC_SNIPER_MAXSPEED; break;
		case PC_SOLDIER: pev->maxspeed = PC_SOLDIER_MAXSPEED; break;
		case PC_DEMOMAN: pev->maxspeed = PC_DEMOMAN_MAXSPEED; break;
		case PC_MEDIC: pev->maxspeed = PC_MEDIC_MAXSPEED; break;
		case PC_HVYWEAP: pev->maxspeed = PC_HVYWEAP_MAXSPEED; break;
		case PC_PYRO: pev->maxspeed = PC_PYRO_MAXSPEED; break;
		case PC_ENGINEER: pev->maxspeed = PC_ENGINEER_MAXSPEED; break;
		case PC_SPY: pev->maxspeed = PC_SPY_MAXSPEED; break;
		case PC_CIVILIAN: pev->maxspeed = PC_CIVILIAN_MAXSPEED; break;
		}

		if ( ( tfstate & TFSTATE_AIMING ) && pev->maxspeed > 80.0f )
			pev->maxspeed = 80.0f;
	}
}

/*
=====================
UTIL_TraceLine

Don't actually trace, but act like the trace didn't hit anything.
=====================
*/
void UTIL_TraceLine( const Vector &vecStart, const Vector &vecEnd, IGNORE_MONSTERS igmon, edict_t *pentIgnore, TraceResult *ptr )
{
	memset( ptr, 0, sizeof( *ptr ) );
	ptr->flFraction = 1.0f;
}

/*
=====================
UTIL_ParticleBox

For debugging, draw a box around a player made out of particles
=====================
*/
void UTIL_ParticleBox( CBasePlayer *player, float *mins, float *maxs, float life, unsigned char r, unsigned char g, unsigned char b )
{
	int i;
	vec3_t mmin, mmax;

	for ( i = 0; i < 3; i++ )
	{
		mmin[i] = player->pev->origin[i] + mins[i];
		mmax[i] = player->pev->origin[i] + maxs[i];
	}

	gEngfuncs.pEfxAPI->R_ParticleBox( (float *)&mmin, (float *)&mmax, 5.0, 0, 255, 0 );
}

/*
=====================
UTIL_ParticleBoxes

For debugging, draw boxes for other collidable players
=====================
*/
void UTIL_ParticleBoxes( void )
{
	int idx;
	physent_t *pe;
	cl_entity_t *player;
	vec3_t mins, maxs;

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );

	// Store off the old count
	gEngfuncs.pEventAPI->EV_PushPMStates();

	player = gEngfuncs.GetLocalPlayer();
	// Now add in all of the players.
	gEngfuncs.pEventAPI->EV_SetSolidPlayers( player->index - 1 );

	for ( idx = 1; idx < 100; idx++ )
	{
		pe = gEngfuncs.pEventAPI->EV_GetPhysent( idx );
		if ( !pe )
			break;

		if ( pe->info >= 1 && pe->info <= gEngfuncs.GetMaxClients() )
		{
			mins = pe->origin + pe->mins;
			maxs = pe->origin + pe->maxs;

			gEngfuncs.pEfxAPI->R_ParticleBox( (float *)&mins, (float *)&maxs, 0, 0, 255, 2.0 );
		}
	}

	gEngfuncs.pEventAPI->EV_PopPMStates();
}

/*
=====================
UTIL_ParticleLine

For debugging, draw a line made out of particles
=====================
*/
void UTIL_ParticleLine( CBasePlayer *player, float *start, float *end, float life, unsigned char r, unsigned char g, unsigned char b )
{
	gEngfuncs.pEfxAPI->R_ParticleLine( start, end, r, g, b, life );
}

/*
=====================
HUD_InitClientWeapons

Set up weapons, player and functions needed to run weapons code client-side.
=====================
*/
void HUD_InitClientWeapons( void )
{
	static int initialized = 0;
	if ( initialized )
		return;

	initialized = 1;

	// Set up pointer ( dummy object )
	gpGlobals = &Globals;

	// Fill in current time ( probably not needed )
	gpGlobals->time = gEngfuncs.GetClientTime();

	// Fake functions
	g_engfuncs.pfnPrecacheModel = stub_PrecacheModel;
	g_engfuncs.pfnPrecacheSound = stub_PrecacheSound;
	g_engfuncs.pfnNameForFunction = stub_NameForFunction;
	g_engfuncs.pfnSetModel = stub_SetModel;
	g_engfuncs.pfnSetClientMaxspeed = HUD_SetMaxSpeed;

	// Handled locally
	g_engfuncs.pfnPlaybackEvent = HUD_PlaybackEvent;
	g_engfuncs.pfnAlertMessage = AlertMessage;

	// Pass through to engine
	g_engfuncs.pfnPrecacheEvent = gEngfuncs.pfnPrecacheEvent;
	g_engfuncs.pfnRandomFloat = gEngfuncs.pfnRandomFloat;
	g_engfuncs.pfnRandomLong = gEngfuncs.pfnRandomLong;

	// Allocate a slot for the local player
	HUD_PrepEntity( &player, NULL, 0 );
	player.tfstate = 0;

	// Allocate slot(s) for each weapon that we are going to be predicting
	HUD_PrepEntity( &g_Gun, &player, 1 );
	HUD_PrepEntity( &g_Super, &player, 1 );
	HUD_PrepEntity( &g_NG, &player, 3 );
	HUD_PrepEntity( &g_SNG, &player, 3 );
	HUD_PrepEntity( &g_Tranq, &player, 3 );
	HUD_PrepEntity( &g_Flame, &player, 2 );
	HUD_PrepEntity( &g_IC, &player, 4 );
	HUD_PrepEntity( &g_Rail, &player, 3 );
	HUD_PrepEntity( &g_RPG, &player, 4 );
	HUD_PrepEntity( &g_AR, &player, 1 );
	HUD_PrepEntity( &g_AC, &player, 1 );
	HUD_PrepEntity( &g_GL, &player, 4 );
	HUD_PrepEntity( &g_PL, &player, 4 );
	HUD_PrepEntity( &g_Sniper, &player, 0 );
	HUD_PrepEntity( &g_Knife, &player, 0 );
	HUD_PrepEntity( &g_Spanner, &player, 0 );
	HUD_PrepEntity( &g_Medkit, &player, 0 );
	HUD_PrepEntity( &g_Crowbar, &player, 0 );
	HUD_PrepEntity( &g_Spot, NULL, 0 );

	g_Tranq.pev->body = 1;
	g_Sniper.m_fInZoom = 0;
	g_Sniper.m_iSpotActive = 0;
	g_Sniper.m_pSpot = NULL;
	g_Sniper.m_fAimedDamage = 0.0f;
	g_Sniper.m_fNextAimBonus = 0.0f;
	g_Spot.pev->effects |= EF_NODRAW;
}

int HUD_NeedSpot( int *damage )
{
	*damage = g_laserdot.laserdotintensity;
	return g_laserdot.laserdotactive;
}

int HUD_CreateSniperDot( int damage, Vector p_viewangles, Vector p_origin, float *dotorigin )
{
	static cl_entity_t dot;
	static float fLastTime, fZAdjust;
	int index;
	Vector forward, right, up;
	Vector farpoint;
	pmtrace_t tr;
	int contents;

	memset( &dot, 0, sizeof( cl_entity_t ) );

	if ( !Bench_Active() )
		dot.model = gEngfuncs.CL_LoadModel( "sprites/laserdot.spr", &index );
	else
		dot.model = gEngfuncs.CL_LoadModel( "sprites/ppdemodot.spr", &index );

	if ( !dot.model || !gEngfuncs.GetLocalPlayer() )
		return FALSE;

	dot.curstate.modelindex = index;
	dot.curstate.movetype = MOVETYPE_NONE;
	dot.curstate.solid = SOLID_NOT;
	dot.curstate.rendermode = kRenderGlow;
	dot.curstate.renderfx = kRenderFxNoDissipation;
	dot.curstate.renderamt = damage;

	gEngfuncs.pfnAngleVectors( p_viewangles, forward, right, up );

	farpoint = forward * 8192.0f + p_origin;

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetSolidPlayers( gEngfuncs.GetLocalPlayer()->index - 1 );
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( p_origin, farpoint, PM_STUDIO_BOX, -1, &tr );

	if ( tr.fraction != 1.0f )
	{
		contents = gEngfuncs.PM_PointContents( tr.endpos, 0 );

		if ( contents != CONTENTS_SKY && ( contents == CONTENTS_WATER ) == ( gEngfuncs.PM_PointContents( p_origin, 0 ) == CONTENTS_WATER ) )
		{
			// Velaron: TODO
			if ( Bench_Active() && Bench_InStage( THIRD_STAGE ) )
			{
				if ( Bench_GetPowerPlay() )
				{

				}
				else
				{

				}
			}

			dot.origin = tr.endpos;
			dot.curstate.origin = tr.endpos;
			dot.prevstate = dot.curstate;
			gEngfuncs.CL_CreateVisibleEntity( ET_NORMAL, &dot );
			*(Vector *)dotorigin = dot.origin;
		}
	}

	gEngfuncs.pEventAPI->EV_PopPMStates();

	return TRUE;
}

/*
=====================
HUD_GetLastOrg

Retruns the last position that we stored for egon beam endpoint.
=====================
*/
void HUD_GetLastOrg( float *org )
{
	int i;

	// Return last origin
	for ( i = 0; i < 3; i++ )
	{
		org[i] = g_laserdot.previousorigin[i];
	}
}

/*
=====================
HUD_SetLastOrg

Remember our exact predicted origin so we can draw the egon to the right position.
=====================
*/
void HUD_SetLastOrg( void )
{
	int i;

	// Offset final origin by view_offset
	for ( i = 0; i < 3; i++ )
	{
		g_laserdot.previousorigin[i] = g_finalstate->playerstate.origin[i] + g_finalstate->client.view_ofs[i];
	}
}

BOOL HUD_SpotActive( void )
{
	return !( g_Spot.pev->effects & EF_NODRAW );
}

/*
=====================
HUD_WeaponsPostThink

Run Weapon firing code on client
=====================
*/
void HUD_WeaponsPostThink( local_state_s *from, local_state_s *to, usercmd_t *cmd, double time, unsigned int random_seed )
{
	int i;
	int buttonsChanged;
	CBasePlayerWeapon *pWeapon = NULL;
	CBasePlayerWeapon *pCurrent;
	weapon_data_t nulldata = { 0 }, *pfrom, *pto;
	static int lasthealth;

	HUD_InitClientWeapons();

	// Get current clock
	gpGlobals->time = time;

	// Fill in data based on selected weapon
	// FIXME, make this a method in each weapon?  where you pass in an entity_state_t *?
	switch ( from->client.m_iId )
	{
	case WEAPON_MEDIKIT:
		pWeapon = &g_Medkit;
		break;
	case WEAPON_SPANNER:
		pWeapon = &g_Spanner;
		break;
	case WEAPON_AXE:
		pWeapon = &g_Crowbar;
		break;
	case WEAPON_SNIPER_RIFLE:
		pWeapon = &g_Sniper;
		break;
	case WEAPON_AUTO_RIFLE:
		pWeapon = &g_AR;
		break;
	case WEAPON_TF_SHOTGUN:
		pWeapon = &g_Gun;
		break;
	case WEAPON_SUPER_SHOTGUN:
		pWeapon = &g_Super;
		break;
	case WEAPON_NAILGUN:
		pWeapon = &g_NG;
		break;
	case WEAPON_SUPER_NAILGUN:
		pWeapon = &g_SNG;
		break;
	case WEAPON_GRENADE_LAUNCHER:
		pWeapon = &g_GL;
		break;
	case WEAPON_FLAMETHROWER:
		pWeapon = &g_Flame;
		break;
	case WEAPON_ROCKET_LAUNCHER:
		pWeapon = &g_RPG;
		break;
	case WEAPON_INCENDIARY:
		pWeapon = &g_IC;
		break;
	case WEAPON_ASSAULT_CANNON:
		pWeapon = &g_AC;
		break;
	case WEAPON_TRANQ:
		pWeapon = &g_Tranq;
		break;
	case WEAPON_LASER:
		pWeapon = &g_Rail;
		break;
	case WEAPON_PIPEBOMB_LAUNCHER:
		pWeapon = &g_PL;
		break;
	case WEAPON_KNIFE:
		pWeapon = &g_Knife;
		break;
	}

	// Store pointer to our destination entity_state_t so we can get our origin, etc. from it
	//  for setting up events on the client
	g_finalstate = to;

	// If we are running events/etc. go ahead and see if we
	//  managed to die between last frame and this one
	// If so, run the appropriate player killed or spawn function
	if ( g_runfuncs )
	{
		if ( to->client.health <= 0 && lasthealth > 0 )
		{
			player.Killed( NULL, NULL, GIB_NORMAL );
		}
		else if ( to->client.health > 0 && lasthealth <= 0 )
		{
			player.Spawn();
		}

		lasthealth = to->client.health;
	}

	// We are not predicting the current weapon, just bow out here.
	if ( !pWeapon )
		return;

	for ( i = 0; i < 32; i++ )
	{
		pCurrent = g_pWpns[i];
		if ( !pCurrent )
		{
			continue;
		}

		pfrom = &from->weapondata[i];

		pCurrent->m_flNextReload = pfrom->m_flNextReload;
		pCurrent->m_fInReload = pfrom->m_fInReload;
		pCurrent->m_fInSpecialReload = pfrom->m_fInSpecialReload;
		pCurrent->m_flPumpTime = pfrom->m_flPumpTime;
		pCurrent->m_iWeaponState = pfrom->m_iWeaponState;
		pCurrent->m_fAimedDamage = pfrom->m_fAimedDamage;
		pCurrent->m_fNextAimBonus = pfrom->m_fNextAimBonus;
		pCurrent->m_fInZoom = pfrom->m_fInZoom;
		pCurrent->m_iClip = pfrom->m_iClip;
		pCurrent->m_flNextPrimaryAttack = pfrom->m_flNextPrimaryAttack;
		pCurrent->m_flNextSecondaryAttack = pfrom->m_flNextSecondaryAttack;
		pCurrent->m_flTimeWeaponIdle = pfrom->m_flTimeWeaponIdle;
		/*
				pCurrent->pev->fuser1 = pfrom->fuser1;
				pCurrent->m_iSecondaryAmmoType = (int)from->client.vuser3[2];
				pCurrent->m_iPrimaryAmmoType = (int)from->client.vuser4[0];
				player.m_rgAmmo[pCurrent->m_iPrimaryAmmoType] = (int)from->client.vuser4[1];
				player.m_rgAmmo[pCurrent->m_iSecondaryAmmoType] = (int)from->client.vuser4[2];
		*/
	}

	// For random weapon events, use this seed to seed random # generator
	player.random_seed = random_seed;

	// Get old buttons from previous state.
	player.m_afButtonLast = from->playerstate.oldbuttons;

	// Which buttsons have changed
	buttonsChanged = ( player.m_afButtonLast ^ cmd->buttons ); // These buttons have changed this frame

	// Debounced button codes for pressed/released
	// The changed ones still down are "pressed"
	player.m_afButtonPressed = buttonsChanged & cmd->buttons;
	// The ones not down are "released"
	player.m_afButtonReleased = buttonsChanged & ( ~cmd->buttons );

	// Set player variables that weapons code might check/alter
	player.pev->button = cmd->buttons;

	player.pev->playerclass = from->playerstate.playerclass;

	player.pev->velocity = from->client.velocity;
	player.pev->flags = from->client.flags;

	player.pev->deadflag = from->client.deadflag;
	player.pev->waterlevel = from->client.waterlevel;
	player.pev->maxspeed = from->client.maxspeed;
	player.tfstate = from->client.tfstate;
	player.pev->fov = from->client.fov;
	player.pev->weaponanim = from->client.weaponanim;
	player.pev->viewmodel = from->client.viewmodel;
	player.m_flNextAttack = from->client.m_flNextAttack;

	//Stores all our ammo info, so the client side weapons can use them.
	player.ammo_nails = from->client.ammo_nails; //is an int anyways...
	player.ammo_shells = from->client.ammo_shells;
	player.ammo_rockets = from->client.ammo_rockets;
	player.ammo_cells = from->client.ammo_cells;

	// Point to current weapon object
	if ( from->client.m_iId )
	{
		player.m_pActiveItem = g_pWpns[from->client.m_iId];
	}

	// Don't go firing anything if we have died.
	// Or if we don't have a weapon model deployed
	if ( ( player.pev->deadflag != ( DEAD_DISCARDBODY + 1 ) ) && !CL_IsDead() && player.pev->viewmodel && !g_iUser1 && player.m_flNextAttack <= 0.0 )
	{
		pWeapon->ItemPostFrame();
	}

	// Assume that we are not going to switch weapons
	to->client.m_iId = from->client.m_iId;

	// Now see if we issued a changeweapon command ( and we're not dead )
	if ( cmd->weaponselect && ( player.pev->deadflag != ( DEAD_DISCARDBODY + 1 ) ) )
	{
		// Switched to a different weapon?
		if ( from->weapondata[cmd->weaponselect].m_iId == cmd->weaponselect )
		{
			CBasePlayerWeapon *pNew = g_pWpns[cmd->weaponselect];
			if ( pNew && ( pNew != pWeapon ) )
			{
				// Put away old weapon
				if ( player.m_pActiveItem )
					player.m_pActiveItem->Holster();

				player.m_pLastItem = player.m_pActiveItem;
				player.m_pActiveItem = pNew;

				// Deploy new weapon
				if ( player.m_pActiveItem )
				{
					player.m_pActiveItem->Deploy();
				}

				// Update weapon id so we can predict things correctly.
				to->client.m_iId = cmd->weaponselect;
			}
		}
	}

	// Store off the last position from the predicted state.
	HUD_SetLastOrg();

	// Wipe it so we can't use it after this frame
	g_finalstate = NULL;

	if ( g_runfuncs )
	{
		g_laserdot.laserdotactive = false;
		g_laserdot.laserdotintensity = 0;

		if ( to->client.m_iId == WEAPON_SNIPER_RIFLE )
		{
			if ( HUD_SpotActive() )
			{
				if ( !CL_IsDead() && ( cmd->buttons & IN_ATTACK ) )
				{
					g_laserdot.laserdotactive = true;
					g_laserdot.laserdotintensity = 0.25f * pWeapon->m_fAimedDamage + 150.0f;
				}
			}
		}
	}
	else
	{
		g_laserdot.laserdotactive = false;
	}

	to->playerstate.playerclass = player.pev->playerclass;
	to->client.tfstate = player.tfstate;

	// Copy in results of prediction code
	to->client.viewmodel = player.pev->viewmodel;
	to->client.fov = player.pev->fov;
	to->client.weaponanim = player.pev->weaponanim;
	to->client.m_flNextAttack = player.m_flNextAttack;
	to->client.maxspeed = player.pev->maxspeed;

	//HL Weapons
	to->client.ammo_nails = player.ammo_nails;
	to->client.ammo_shells = player.ammo_shells;
	to->client.ammo_cells = player.ammo_cells;
	to->client.ammo_rockets = player.ammo_rockets;

	// Make sure that weapon animation matches what the game .dll is telling us
	//  over the wire ( fixes some animation glitches )
	if ( g_runfuncs && ( HUD_GetWeaponAnim() != to->client.weaponanim ) )
	{
		HUD_SendWeaponAnim( to->client.weaponanim, ( pWeapon != &g_Tranq ) + 1, 1 );
	}

	for ( i = 0; i < 32; i++ )
	{
		pCurrent = g_pWpns[i];

		pto = &to->weapondata[i];

		if ( !pCurrent )
		{
			memset( pto, 0, sizeof( weapon_data_t ) );
			continue;
		}

		pto->m_flNextReload = pCurrent->m_flNextReload;
		pto->m_fInReload = pCurrent->m_fInReload;
		pto->m_fInSpecialReload = pCurrent->m_fInSpecialReload;
		pto->m_flPumpTime = pCurrent->m_flPumpTime;
		pto->m_iWeaponState = pCurrent->m_iWeaponState;
		pto->m_fAimedDamage = pCurrent->m_fAimedDamage;
		pto->m_fNextAimBonus = pCurrent->m_fNextAimBonus;
		pto->m_fInZoom = pCurrent->m_fInZoom;
		pto->m_iClip = pCurrent->m_iClip;
		pto->m_flNextPrimaryAttack = pCurrent->m_flNextPrimaryAttack;
		pto->m_flNextSecondaryAttack = pCurrent->m_flNextSecondaryAttack;
		pto->m_flTimeWeaponIdle = pCurrent->m_flTimeWeaponIdle;
		//pto->fuser1 = pCurrent->pev->fuser1;

		// Decrement weapon counters, server does this at same time ( during post think, after doing everything else )
		pto->m_flNextReload -= cmd->msec / 1000.0f;
		pto->m_fNextAimBonus -= cmd->msec / 1000.0f;
		pto->m_flNextPrimaryAttack -= cmd->msec / 1000.0f;
		pto->m_flNextSecondaryAttack -= cmd->msec / 1000.0f;
		pto->m_flTimeWeaponIdle -= cmd->msec / 1000.0f;
		//pto->fuser1 -= cmd->msec / 1000.0f;

		/*
		to->client.vuser3[2] = pCurrent->m_iSecondaryAmmoType;
		to->client.vuser4[0] = pCurrent->m_iPrimaryAmmoType;
		to->client.vuser4[1] = player.m_rgAmmo[pCurrent->m_iPrimaryAmmoType];
		to->client.vuser4[2] = player.m_rgAmmo[pCurrent->m_iSecondaryAmmoType];
*/

		if ( pto->m_flPumpTime != -9999.0f )
		{
			pto->m_flPumpTime -= cmd->msec / 1000.0f;
			if ( pto->m_flPumpTime < -0.001f )
				pto->m_flPumpTime = -0.001f;
		}

		if ( pto->m_fNextAimBonus < -1.0f )
		{
			pto->m_fNextAimBonus = -1.0f;
		}

		if ( pto->m_flNextPrimaryAttack < -1.0f )
		{
			pto->m_flNextPrimaryAttack = -1.0f;
		}

		if ( pto->m_flNextSecondaryAttack < -0.001f )
		{
			pto->m_flNextSecondaryAttack = -0.001f;
		}

		if ( pto->m_flTimeWeaponIdle < -0.001f )
		{
			pto->m_flTimeWeaponIdle = -0.001f;
		}

		if ( pto->m_flNextReload < -0.001f )
		{
			pto->m_flNextReload = -0.001f;
		}

		//if ( pto->fuser1 < -0.001f )
		//{
		//	pto->fuser1 = -0.001f;
		//}
	}

	// m_flNextAttack is now part of the weapons, but is part of the player instead
	to->client.m_flNextAttack -= cmd->msec / 1000.0f;
	if ( to->client.m_flNextAttack < -0.001f )
	{
		to->client.m_flNextAttack = -0.001f;
	}

	/*
		to->client.fuser2 -= cmd->msec / 1000.0f;
		if( to->client.fuser2 < -0.001f )
		{
			to->client.fuser2 = -0.001f;
		}

		to->client.fuser3 -= cmd->msec / 1000.0f;
		if( to->client.fuser3 < -0.001f )
		{
			to->client.fuser3 = -0.001f;
		}
	*/
}

/*
=====================
HUD_PostRunCmd

Client calls this during prediction, after it has moved the player and updated any info changed into to->
time is the current client clock based on prediction
cmd is the command that caused the movement, etc
runfuncs is 1 if this is the first time we've predicted this command.  If so, sounds and effects should play, otherwise, they should
be ignored
=====================
*/
void _DLLEXPORT HUD_PostRunCmd( struct local_state_s *from, struct local_state_s *to, struct usercmd_s *cmd, int runfuncs, double time, unsigned int random_seed )
{
	g_runfuncs = runfuncs;

#if defined( CLIENT_WEAPONS )
	if ( cl_lw && cl_lw->value )
	{
		if ( !to->client.iuser4 )
			HUD_WeaponsPostThink( from, to, cmd, time, random_seed );
		g_lastFOV = to->client.fov;
	}
	else
#endif
	{
		to->client.fov = g_lastFOV;
		g_laserdot.laserdotactive = 0;
	}
}
