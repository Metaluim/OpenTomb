
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_platform.h>
#include <SDL2/SDL_opengl.h>

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"

#include "audio.h"
#include "vmath.h"
#include "polygon.h"
#include "camera.h"
#include "portal.h"
#include "frustum.h"
#include "world.h"
#include "mesh.h"
#include "entity.h"
#include "render.h"
#include "engine.h"
#include "script.h"
#include "obb.h"
#include "redblack.h"
#include "console.h"
#include "resource.h"
#include "bsp_tree.h"

void Room_Empty(room_p room)
{
    portal_p p;
    btRigidBody* body;

    if(room == NULL)
    {
        return;
    }

    room->containers = NULL;

    p = room->portals;
    room->near_room_list_size = 0;

    if(room->portal_count)
    {
        for(uint16_t i=0;i<room->portal_count;i++,p++)
        {
            Portal_Clear(p);
        }
        free(room->portals);
        room->portals = NULL;
        room->portal_count = 0;
    }

    Frustum_Delete(room->frustum);
    room->frustum = NULL;

    if(room->mesh)
    {
        BaseMesh_Clear(room->mesh);
        free(room->mesh);
        room->mesh = NULL;
    }

    if(room->static_mesh_count)
    {
        for(uint32_t i=0;i<room->static_mesh_count;i++)
        {
            body = room->static_mesh[i].bt_body;
            if(body)
            {
                body->setUserPointer(NULL);
                if(body->getMotionState())
                {
                    delete body->getMotionState();
                    body->setMotionState(NULL);
                }
                if(body->getCollisionShape())
                {
                    delete body->getCollisionShape();
                    body->setCollisionShape(NULL);
                }

                bt_engine_dynamicsWorld->removeRigidBody(body);
                delete body;
                room->static_mesh[i].bt_body = NULL;
            }

            OBB_Clear(room->static_mesh[i].obb);
            free(room->static_mesh[i].obb);
            room->static_mesh[i].obb = NULL;
            if(room->static_mesh[i].self)
            {
                room->static_mesh[i].self->room = NULL;
                free(room->static_mesh[i].self);
                room->static_mesh[i].self = NULL;
            }
        }
        free(room->static_mesh);
        room->static_mesh = NULL;
        room->static_mesh_count = 0;
    }

    if(room->bt_body)
    {
        body = room->bt_body;
        if(body)
        {
            body->setUserPointer(NULL);
            if(body->getMotionState())
            {
                delete body->getMotionState();
                body->setMotionState(NULL);
            }
            if(body->getCollisionShape())
            {
                delete body->getCollisionShape();
                body->setCollisionShape(NULL);
            }

            bt_engine_dynamicsWorld->removeRigidBody(body);
            delete body;
            room->bt_body = NULL;
        }
    }

    if(room->sectors_count)
    {
        free(room->sectors);
        room->sectors = NULL;
        room->sectors_count = 0;
        room->sectors_x = 0;
        room->sectors_y = 0;
    }

    if(room->sprites_count)
    {
        free(room->sprites);
        room->sprites = NULL;
        room->sprites_count = 0;
    }

    if(room->light_count)
    {
        free(room->lights);
        room->lights = NULL;
        room->light_count = 0;
    }

    if(room->self)
    {
        room->self->room = NULL;
        free(room->self);
        room->self = NULL;
    }
}


void Room_AddEntity(room_p room, struct entity_s *entity)
{
    engine_container_p curr;

    for(curr=room->containers;curr!=NULL;curr=curr->next)
    {
        if(curr == entity->self)
        {
            return;
        }
    }

    entity->self->room = room;
    entity->self->next = room->containers;
    room->containers = entity->self;
}


int Room_RemoveEntity(room_p room, struct entity_s *entity)
{
    engine_container_p previous_cont, current_cont;

    if((entity == NULL) || (room->containers == NULL))
    {
        return 0;
    }

    if(room->containers == entity->self)
    {
        room->containers = entity->self->next;
        entity->self->room = NULL;
        return 1;
    }

    previous_cont = room->containers;
    current_cont = previous_cont->next;
    for(;current_cont!=NULL;)
    {
        if(current_cont == entity->self)
        {
            previous_cont->next = current_cont->next;
            entity->self->room = NULL;
            return 1;
        }

        previous_cont = current_cont;
        current_cont = current_cont->next;
    }

    return 0;
}


void Room_AddToNearRoomsList(room_p room, room_p r)
{
    if(room && r && !Room_IsInNearRoomsList(room, r) && room->id != r->id && !Room_IsOverlapped(room, r) && room->near_room_list_size < 64)
    {
        room->near_room_list[room->near_room_list_size] = r;
        room->near_room_list_size++;
    }
}


int Room_IsInNearRoomsList(room_p r0, room_p r1)
{
    if(r0 && r1)
    {
        if(r0->id == r1->id)
        {
            return 1;
        }

        if(r1->near_room_list_size >= r0->near_room_list_size)
        {
            for(uint16_t i=0;i<r0->near_room_list_size;i++)
            {
                if(r0->near_room_list[i]->id == r1->id)
                {
                    return 1;
                }
            }
        }
        else
        {
            for(uint16_t i=0;i<r1->near_room_list_size;i++)
            {
                if(r1->near_room_list[i]->id == r0->id)
                {
                    return 1;
                }
            }
        }
    }

    return 0;
}


int Room_HasSector(room_p room, int x, int y)
{
    if(x < room->sectors_x && y < room->sectors_y )
    {
        return 1;
    }

    return 0;
}


room_sector_p TR_Sector_CheckPortalPointerRaw(room_sector_p rs)
{
    if((rs != NULL) && (rs->portal_to_room >= 0))
    {
        room_p r = engine_world.rooms + rs->portal_to_room;
        int ind_x = (rs->pos[0] - r->transform[12 + 0]) / TR_METERING_SECTORSIZE;
        int ind_y = (rs->pos[1] - r->transform[12 + 1]) / TR_METERING_SECTORSIZE;
        if((ind_x >= 0) && (ind_x < r->sectors_x) && (ind_y >= 0) && (ind_y < r->sectors_y))
        {
            rs = r->sectors + (ind_x * r->sectors_y + ind_y);
        }
    }

    return rs;
}


room_sector_p TR_Sector_CheckPortalPointer(room_sector_p rs)
{
    if((rs != NULL) && (rs->portal_to_room >= 0))
    {
        room_p r = engine_world.rooms + rs->portal_to_room;
        if((rs->owner_room->base_room != NULL) && (r->alternate_room != NULL))
        {
            r = r->alternate_room;
        }
        else if((rs->owner_room->alternate_room != NULL) && (r->base_room != NULL))
        {
            r = r->base_room;
        }
        int ind_x = (rs->pos[0] - r->transform[12 + 0]) / TR_METERING_SECTORSIZE;
        int ind_y = (rs->pos[1] - r->transform[12 + 1]) / TR_METERING_SECTORSIZE;
        if((ind_x >= 0) && (ind_x < r->sectors_x) && (ind_y >= 0) && (ind_y < r->sectors_y))
        {
            rs = r->sectors + (ind_x * r->sectors_y + ind_y);
        }
    }

    return rs;
}


room_sector_p TR_Sector_CheckBaseRoom(room_sector_p rs)
{
    if((rs != NULL) && (rs->owner_room->base_room != NULL))
    {
        room_p r = rs->owner_room->base_room;
        int ind_x = (rs->pos[0] - r->transform[12 + 0]) / TR_METERING_SECTORSIZE;
        int ind_y = (rs->pos[1] - r->transform[12 + 1]) / TR_METERING_SECTORSIZE;
        if((ind_x >= 0) && (ind_x < r->sectors_x) && (ind_y >= 0) && (ind_y < r->sectors_y))
        {
            rs = r->sectors + (ind_x * r->sectors_y + ind_y);
        }
    }

    return rs;
}


room_sector_p TR_Sector_CheckAlternateRoom(room_sector_p rs)
{
    if((rs != NULL) && (rs->owner_room->alternate_room != NULL))
    {
        room_p r = rs->owner_room->alternate_room;
        int ind_x = (rs->pos[0] - r->transform[12 + 0]) / TR_METERING_SECTORSIZE;
        int ind_y = (rs->pos[1] - r->transform[12 + 1]) / TR_METERING_SECTORSIZE;
        if((ind_x >= 0) && (ind_x < r->sectors_x) && (ind_y >= 0) && (ind_y < r->sectors_y))
        {
            rs = r->sectors + (ind_x * r->sectors_y + ind_y);
        }
    }

    return rs;
}


int Sectors_Is2SidePortals(room_sector_p s1, room_sector_p s2)
{
    s1 = TR_Sector_CheckPortalPointer(s1);
    s2 = TR_Sector_CheckPortalPointer(s2);

    if(s1->owner_room == s2->owner_room)
    {
        return 0;
    }

    room_sector_p s1p = Room_GetSectorRaw(s2->owner_room, s1->pos);
    room_sector_p s2p = Room_GetSectorRaw(s1->owner_room, s2->pos);

    // 2 next conditions are the stick for TR_V door-roll-wall
    if(s1p->portal_to_room < 0)
    {
        s1p = TR_Sector_CheckAlternateRoom(s1p);
        if(s1p->portal_to_room < 0)
        {
            return 0;
        }
    }
    if(s2p->portal_to_room < 0)
    {
        s2p = TR_Sector_CheckAlternateRoom(s2p);
        if(s2p->portal_to_room < 0)
        {
            return 0;
        }
    }

    if((TR_Sector_CheckPortalPointer(s1p) == TR_Sector_CheckBaseRoom(s1)) && (TR_Sector_CheckPortalPointer(s2p) == TR_Sector_CheckBaseRoom(s2)) ||
       (TR_Sector_CheckPortalPointer(s1p) == TR_Sector_CheckAlternateRoom(s1)) && (TR_Sector_CheckPortalPointer(s2p) == TR_Sector_CheckAlternateRoom(s2)))
    {
        return 1;
    }

    return 0;
}


int Room_IsOverlapped(room_p r0, room_p r1)
{
    if(r0->bb_min[0] >= r1->bb_max[0] || r0->bb_max[0] <= r1->bb_min[0] ||
       r0->bb_min[1] >= r1->bb_max[1] || r0->bb_max[1] <= r1->bb_min[1] ||
       r0->bb_min[2] >= r1->bb_max[2] || r0->bb_max[2] <= r1->bb_min[2])
    {
        return 0;
    }

    return !Room_IsJoined(r0, r1);
}


void World_Prepare(world_p world)
{
    world->id = 0;
    world->name = NULL;
    world->type = 0x00;
    world->meshes = NULL;
    world->meshes_count = 0;
    world->sprites = NULL;
    world->sprites_count = 0;
    world->room_count = 0;
    world->rooms = 0;
    world->flip_map = NULL;
    world->flip_state = NULL;
    world->flip_count = 0;
    world->textures = NULL;
    world->type = 0;
    world->entity_tree = NULL;
    world->items_tree = NULL;
    world->Character = NULL;

    world->audio_sources = NULL;
    world->audio_sources_count = 0;
    world->audio_buffers = NULL;
    world->audio_buffers_count = 0;
    world->audio_effects = NULL;
    world->audio_effects_count = 0;
    world->anim_sequences = NULL;
    world->anim_sequences_count = 0;
    world->stream_tracks = NULL;
    world->stream_tracks_count = 0;
    world->stream_track_map = NULL;
    world->stream_track_map_count = 0;

    world->tex_count = 0;
    world->textures = 0;
    world->room_boxes = NULL;
    world->room_box_count = 0;
    world->cameras_sinks = NULL;
    world->cameras_sinks_count = 0;
    world->skeletal_models = NULL;
    world->skeletal_model_count = 0;
    world->sky_box = NULL;
    world->anim_commands = NULL;
    world->anim_commands_count = 0;
}


void World_Empty(world_p world)
{
    extern engine_container_p last_cont;

    last_cont = NULL;
    Engine_LuaClearTasks();
    // De-initialize and destroy all audio objects.
    Audio_DeInit();

    if(main_inventory_manager != NULL)
    {
        main_inventory_manager->setInventory(NULL);
        main_inventory_manager->setItemsType(1);                                // see base items
    }

    if(world->Character != NULL)
    {
        world->Character->self->room = NULL;
        world->Character->self->next = NULL;
        world->Character->current_sector = NULL;
    }

    /* entity empty must be done before rooms destroy */
    RB_Free(world->entity_tree);
    world->entity_tree = NULL;

    /* Now we can delete bullet misc */
    if(bt_engine_dynamicsWorld != NULL)
    {
        for(int i=bt_engine_dynamicsWorld->getNumCollisionObjects()-1;i>=0;i--)
        {
            btCollisionObject* obj = bt_engine_dynamicsWorld->getCollisionObjectArray()[i];
            btRigidBody* body = btRigidBody::upcast(obj);
            if(body != NULL)
            {
                engine_container_p cont = (engine_container_p)body->getUserPointer();
                body->setUserPointer(NULL);

                if(cont && (cont->object_type == OBJECT_BULLET_MISC))
                {
                    if(body->getMotionState())
                    {
                        delete body->getMotionState();
                        body->setMotionState(NULL);
                    }

                    if(body->getCollisionShape())
                    {
                        delete body->getCollisionShape();
                        body->setCollisionShape(NULL);
                    }

                    if(body->isInWorld())
                    {
                        bt_engine_dynamicsWorld->removeRigidBody(body);
                    }

                    cont->room = NULL;
                    free(cont);
                    delete body;
                }
            }
        }
    }

    for(uint32_t i=0;i<world->room_count;i++)
    {
        Room_Empty(world->rooms+i);
    }
    free(world->rooms);
    world->rooms = NULL;

    free(world->flip_map);
    free(world->flip_state);
    world->flip_map = NULL;
    world->flip_state = NULL;
    world->flip_count = 0;

    if(world->room_box_count)
    {
        free(world->room_boxes);
        world->room_boxes = NULL;
        world->room_box_count = 0;
    }

    if(world->cameras_sinks_count)
    {
        free(world->cameras_sinks);
        world->cameras_sinks = NULL;
        world->cameras_sinks_count = 0;
    }

    /*sprite empty*/
    if(world->sprites_count)
    {
        free(world->sprites);
        world->sprites = NULL;
        world->sprites_count = 0;
    }

    /*items empty*/
    RB_Free(world->items_tree);
    world->items_tree = NULL;

    if(world->Character)
    {
        Entity_Clear(world->Character);
        free(world->Character);
        world->Character = NULL;
    }

    if(world->skeletal_model_count)
    {
        for(uint32_t i=0;i<world->skeletal_model_count;i++)
        {
            SkeletalModel_Clear(world->skeletal_models+i);
        }
        free(world->skeletal_models);
        world->skeletal_models = NULL;
        world->skeletal_model_count = 0;
    }

    /*mesh empty*/

    if(world->meshes_count)
    {
        for(uint32_t i=0;i<world->meshes_count;i++)
        {
            BaseMesh_Clear(world->meshes+i);
        }
        free(world->meshes);
        world->meshes = NULL;
        world->meshes_count = 0;
    }

    if(world->tex_count)
    {
        glDeleteTextures(world->tex_count ,world->textures);
        world->tex_count = 0;
        free(world->textures);
        world->textures = NULL;
    }

    if(world->tex_atlas)
    {
        delete world->tex_atlas;
        world->tex_atlas = NULL;
    }

    if(world->anim_sequences_count)
    {
        for(uint32_t i=0;i < world->anim_sequences_count;i++)
        {
            if(world->anim_sequences[i].frames_count != 0)
            {
                free(world->anim_sequences[i].frame_list);
                world->anim_sequences[i].frame_list = NULL;
                free(world->anim_sequences[i].frames);
                world->anim_sequences[i].frames = NULL;
            }
            world->anim_sequences[i].frames_count = 0;
        }
        world->anim_sequences_count = 0;
        free(world->anim_sequences);
        world->anim_sequences = NULL;
    }
}


int compEntityEQ(void *x, void *y)
{
    return (*((uint32_t*)x) == *((uint32_t*)y));
}


int compEntityLT(void *x, void *y)
{
    return (*((uint32_t*)x) < *((uint32_t*)y));
}


void RBEntityFree(void *x)
{
    Entity_Clear((entity_p)x);
    free(x);
}


void RBItemFree(void *x)
{
    free(((base_item_p)x)->bf->bone_tags);
    free(((base_item_p)x)->bf);
    free(x);
}


uint32_t World_SpawnEntity(uint32_t model_id, uint32_t room_id, btScalar pos[3], btScalar ang[3], int32_t id)
{
    if(engine_world.entity_tree != NULL)
    {
        skeletal_model_p model = World_GetModelByID(&engine_world, model_id);
        if(model != NULL)
        {
            entity_p ent = World_GetEntityByID(&engine_world, id);
            RedBlackNode_p node = engine_world.entity_tree->root;

            if(ent != NULL)
            {
                if(pos != NULL)
                {
                    vec3_copy(ent->transform+12, pos);
                }
                if(ang != NULL)
                {
                    vec3_copy(ent->angles, ang);
                    Entity_UpdateRotation(ent);
                }
                if(room_id < engine_world.room_count)
                {
                    ent->self->room = engine_world.rooms + room_id;
                    ent->current_sector = Room_GetSectorRaw(ent->self->room, ent->transform+12);
                }
                else
                {
                    ent->self->room = NULL;
                }

                return ent->id;
            }

            ent = Entity_Create();

            if(id < 0)
            {
                ent->id = 0;
                while(node != NULL)
                {
                    ent->id = *((uint32_t*)node->key) + 1;
                    node = node->right;
                }
            }
            else
            {
                ent->id = id;
            }

            if(pos != NULL)
            {
                vec3_copy(ent->transform+12, pos);
            }
            if(ang != NULL)
            {
                vec3_copy(ent->angles, ang);
                Entity_UpdateRotation(ent);
            }
            if(room_id < engine_world.room_count)
            {
                ent->self->room = engine_world.rooms + room_id;
                ent->current_sector = Room_GetSectorRaw(ent->self->room, ent->transform+12);
            }
            else
            {
                ent->self->room = NULL;
            }

            ent->type_flags       = ENTITY_TYPE_SPAWNED;
            ent->state_flags      = ENTITY_STATE_ENABLED | ENTITY_STATE_ACTIVE | ENTITY_STATE_VISIBLE;
            ent->activation_mask  = 0x00;
            ent->OCB              = 0x00;
            ent->locked           = 0;
            ent->timer            = 0.0;

            ent->self->collide_flag = 0x00;
            ent->move_type = 0x0000;
            ent->bf.animations.anim_flags = 0x0000;
            ent->bf.animations.current_animation = 0;
            ent->bf.animations.current_frame = 0;
            ent->bf.animations.frame_time = 0.0;
            ent->inertia = 0.0;
            ent->move_type = 0;

            ent->bf.animations.model = model;
            ent->bf.bone_tag_count = model->mesh_count;
            ent->bf.bone_tags = (ss_bone_tag_p)malloc(model->mesh_count * sizeof(ss_bone_tag_t));
            for(uint16_t j=0;j<model->mesh_count;j++)
            {
                ent->bf.bone_tags[j].flag = model->mesh_tree[j].flag;
                ent->bf.bone_tags[j].mesh_base = model->mesh_tree[j].mesh_base;
                ent->bf.bone_tags[j].mesh_skin = model->mesh_tree[j].mesh_skin;
                ent->bf.bone_tags[j].mesh_slot = NULL;

                vec3_copy(ent->bf.bone_tags[j].offset, model->mesh_tree[j].offset);
                vec4_set_zero(ent->bf.bone_tags[j].qrotate);
                Mat4_E_macro(ent->bf.bone_tags[j].transform);
                Mat4_E_macro(ent->bf.bone_tags[j].full_transform);
            }

            Entity_SetAnimation(ent, 0, 0);                                     // Set zero animation and zero frame
            BT_GenEntityRigidBody(ent);

            Entity_RebuildBV(ent);
            if(ent->self->room != NULL)
            {
                Room_AddEntity(ent->self->room, ent);
            }
            World_AddEntity(&engine_world, ent);

            return ent->id;
        }
    }

    return 0xFFFFFFFF;
}


struct entity_s *World_GetEntityByID(world_p world, uint32_t id)
{
    entity_p ent = NULL;
    RedBlackNode_p node;

    if((world == NULL) || (world->entity_tree == NULL))
    {
        return NULL;
    }

    if((world->Character != NULL) && (world->Character->id == id))
    {
        return world->Character;
    }

    node = RB_SearchNode(&id, world->entity_tree);
    if(node != NULL)
    {
        ent = (entity_p)node->data;
    }

    return ent;
}


struct base_item_s *World_GetBaseItemByID(world_p world, uint32_t id)
{
    base_item_p item = NULL;
    RedBlackNode_p node;

    if((world == NULL) || (world->items_tree == NULL))
    {
        return NULL;
    }

    node = RB_SearchNode(&id, world->items_tree);
    if(node != NULL)
    {
        item = (base_item_p)node->data;
    }

    return item;
}


inline int Room_IsPointIn(room_p room, btScalar dot[3])
{
    return (dot[0] >= room->bb_min[0]) && (dot[0] < room->bb_max[0]) &&
           (dot[1] >= room->bb_min[1]) && (dot[1] < room->bb_max[1]) &&
           (dot[2] >= room->bb_min[2]) && (dot[2] < room->bb_max[2]);
}


room_p Room_FindPos(world_p w, btScalar pos[3])
{
    room_p r = w->rooms;
    for(uint32_t i=0;i<w->room_count;i++,r++)
    {
        if(r->active &&
           (pos[0] >= r->bb_min[0]) && (pos[0] < r->bb_max[0]) &&
           (pos[1] >= r->bb_min[1]) && (pos[1] < r->bb_max[1]) &&
           (pos[2] >= r->bb_min[2]) && (pos[2] < r->bb_max[2]))
        {
            return r;
        }
    }
    return NULL;
}


room_p Room_FindPos2d(world_p w, btScalar pos[3])
{
    room_p r = w->rooms;
    for(uint32_t i=0;i<w->room_count;i++,r++)
    {
        if(r->active &&
           (pos[0] >= r->bb_min[0]) && (pos[0] < r->bb_max[0]) &&
           (pos[1] >= r->bb_min[1]) && (pos[1] < r->bb_max[1]))
        {
            return r;
        }
    }
    return NULL;
}


room_p Room_FindPosCogerrence(world_p w, btScalar pos[3], room_p room)
{
    room_p r;

    if(room == NULL)
    {
        return Room_FindPos(w, pos);
    }

    if(room->active &&
       (pos[0] >= room->bb_min[0]) && (pos[0] < room->bb_max[0]) &&
       (pos[1] >= room->bb_min[1]) && (pos[1] < room->bb_max[1]) &&
       (pos[2] >= room->bb_min[2]) && (pos[2] < room->bb_max[2]))
    {
        return room;
    }

    for(uint16_t i=0;i<room->near_room_list_size;i++)
    {
        r = room->near_room_list[i];
        if(r->active &&
           (pos[0] >= r->bb_min[0]) && (pos[0] < r->bb_max[0]) &&
           (pos[1] >= r->bb_min[1]) && (pos[1] < r->bb_max[1]) &&
           (pos[2] >= r->bb_min[2]) && (pos[2] < r->bb_max[2]))
        {
            return r;
        }
    }

    return Room_FindPos(w, pos);
}


room_p Room_FindPosCogerrence2d(world_p w, btScalar pos[3], room_p room)
{
    room_p r;
    if(room == NULL)
    {
        return Room_FindPos2d(w, pos);
    }

    if((pos[0] >= room->bb_min[0]) && (pos[0] < room->bb_max[0]) &&
       (pos[1] >= room->bb_min[1]) && (pos[1] < room->bb_max[1]) &&
       (pos[2] >= room->bb_min[2]) && (pos[2] < room->bb_max[2]))
    {
        return room;
    }

    for(uint16_t i=0;i<room->portal_count;i++)
    {
        r = room->portals[i].dest_room;
        if((pos[0] >= r->bb_min[0]) && (pos[0] < r->bb_max[0]) &&
           (pos[1] >= r->bb_min[1]) && (pos[1] < r->bb_max[1]) &&
           (pos[2] >= r->bb_min[2]) && (pos[2] < r->bb_max[2]))
        {
            return r;
        }
    }
    return Room_FindPos2d(w, pos);
}


room_p Room_GetByID(world_p w, unsigned int ID)
{
    room_p r = w->rooms;
    for(uint32_t i=0;i<w->room_count;i++,r++)
    {
        if(ID == r->id)
        {
            return r;
        }
    }
    return NULL;
}


room_sector_p Room_GetSectorRaw(room_p room, btScalar pos[3])
{
    int x, y;
    room_sector_p ret = NULL;

    if((room == NULL) || !room->active)
    {
        return NULL;
    }

    x = (int)(pos[0] - room->transform[12]) / 1024;
    y = (int)(pos[1] - room->transform[13]) / 1024;
    if(x < 0 || x >= room->sectors_x || y < 0 || y >= room->sectors_y)
    {
        return NULL;
    }
    /*
     * column index system
     * X - column number, Y - string number
     */
    ret = room->sectors + x * room->sectors_y + y;
    return ret;
}


room_sector_p Room_GetSectorCheckFlip(room_p room, btScalar pos[3])
{
    int x, y;
    room_sector_p ret = NULL;

    if(room != NULL)
    {
        if(room->active == 0)
        {
            if((room->base_room != NULL) && (room->base_room->active))
            {
                room = room->base_room;
            }
            else if((room->alternate_room != NULL) && (room->alternate_room->active))
            {
                room = room->alternate_room;
            }
        }
    }
    else
    {
        return NULL;
    }

    if(!room->active)
    {
        return NULL;
    }

    x = (int)(pos[0] - room->transform[12]) / 1024;
    y = (int)(pos[1] - room->transform[13]) / 1024;
    if(x < 0 || x >= room->sectors_x || y < 0 || y >= room->sectors_y)
    {
        return NULL;
    }
    /*
     * column index system
     * X - column number, Y - string number
     */
    ret = room->sectors + x * room->sectors_y + y;
    return ret;
}


room_sector_p Sector_CheckFlip(room_sector_p rs)
{
    if((rs != NULL) && (rs->owner_room->active == 0))
    {
        if((rs->owner_room->base_room != NULL) && (rs->owner_room->base_room->active))
        {
            room_p r = rs->owner_room->base_room;
            rs = r->sectors + rs->index_x * r->sectors_y + rs->index_y;
        }
        else if((rs->owner_room->alternate_room != NULL) && (rs->owner_room->alternate_room->active))
        {
            room_p r = rs->owner_room->alternate_room;
            rs = r->sectors + rs->index_x * r->sectors_y + rs->index_y;
        }
    }

    return rs;
}


room_sector_p Room_GetSectorXYZ(room_p room, btScalar pos[3])
{
    int x, y;
    room_sector_p ret = NULL;

    room = Room_CheckFlip(room);

    if(!room->active)
    {
        return NULL;
    }

    x = (int)(pos[0] - room->transform[12]) / 1024;
    y = (int)(pos[1] - room->transform[13]) / 1024;
    if(x < 0 || x >= room->sectors_x || y < 0 || y >= room->sectors_y)
    {
        return NULL;
    }
    /*
     * column index system
     * X - column number, Y - string number
     */
    ret = room->sectors + x * room->sectors_y + y;

    /*
     * resolve Z overlapped neighboard rooms. room below has more priority.
     */
    if(ret->sector_below && (ret->sector_below->ceiling >= pos[2]))
    {
        return Sector_CheckFlip(ret->sector_below);
    }

    if(ret->sector_above && (ret->sector_above->floor <= pos[2]))
    {
        return Sector_CheckFlip(ret->sector_above);
    }

    return ret;
}


void Room_Enable(room_p room)
{
    if(room->active)
    {
        return;
    }

    if(room->bt_body != NULL)
    {
        bt_engine_dynamicsWorld->addRigidBody(room->bt_body);
    }

    for(uint32_t i=0;i<room->static_mesh_count;i++)
    {
        if(room->static_mesh[i].bt_body != NULL)
        {
            bt_engine_dynamicsWorld->addRigidBody(room->static_mesh[i].bt_body);
        }
    }

    for(engine_container_p cont=room->containers;cont;cont=cont->next)
    {
        switch(cont->object_type)
        {
            case OBJECT_ENTITY:
                Entity_Enable((entity_p)cont->object);
                break;
        }
    }

    room->active = 1;
}


void Room_Disable(room_p room)
{
    if(!room->active)
    {
        return;
    }

    if(room->bt_body != NULL)
    {
        bt_engine_dynamicsWorld->removeRigidBody(room->bt_body);
    }

    for(uint32_t i=0;i<room->static_mesh_count;i++)
    {
        if(room->static_mesh[i].bt_body != NULL)
        {
            bt_engine_dynamicsWorld->removeRigidBody(room->static_mesh[i].bt_body);
        }
    }

    for(engine_container_p cont=room->containers;cont;cont=cont->next)
    {
        switch(cont->object_type)
        {
            case OBJECT_ENTITY:
                Entity_Disable((entity_p)cont->object);
                break;
        }
    }

    room->active = 0;
}

void Room_SwapToBase(room_p room)
{
    if((room->base_room != NULL) && (room->active == 1))                        //If room is active alternate room
    {
        Render_CleanList();
        Room_Disable(room);                             //Disable current room
        Room_Disable(room->base_room);                  //Paranoid
        Room_SwapPortals(room, room->base_room);        //Update portals to match this room
        Room_SwapItems(room, room->base_room);     //Update items to match this room
        Room_Enable(room->base_room);                   //Enable original room
    }
}

void Room_SwapToAlternate(room_p room)
{
    if((room->alternate_room != NULL) && (room->active == 1))              //If room is active base room
    {
        Render_CleanList();
        Room_Disable(room);                             //Disable current room
        Room_Disable(room->alternate_room);             //Paranoid
        Room_SwapPortals(room, room->alternate_room);   //Update portals to match this room
        Room_SwapItems(room, room->alternate_room);          //Update items to match this room
        Room_Enable(room->alternate_room);                              //Enable base room
    }
}

room_p Room_CheckFlip(room_p r)
{
    if((r != NULL) && (r->active == 0))
    {
        if((r->base_room != NULL) && (r->base_room->active))
        {
            r = r->base_room;
        }
        else if((r->alternate_room != NULL) && (r->alternate_room->active))
        {
            r = r->alternate_room;
        }
    }

    return r;
}

void Room_SwapPortals(room_p room, room_p dest_room)
{
    //Update portals in room rooms
    for(uint32_t i=0;i<engine_world.room_count;i++)//For every room in the world itself
    {
        for(uint16_t j=0;j<engine_world.rooms[i].portal_count;j++)//For every portal in this room
        {
            if(engine_world.rooms[i].portals[j].dest_room->id == room->id)//If a portal is linked to the input room
            {
                engine_world.rooms[i].portals[j].dest_room = dest_room;//The portal destination room is the destination room!
                //Con_Printf("The current room %d! has room %d joined to it!", room->id, i);
            }
        }
        Room_BuildNearRoomsList(&engine_world.rooms[i]);//Rebuild room near list!
    }
}

void Room_SwapItems(room_p room, room_p dest_room)
{
    engine_container_p t;

    for(t=room->containers;t!=NULL;t=t->next)
    {
        t->room = dest_room;
    }

    for(t=dest_room->containers;t!=NULL;t=t->next)
    {
        t->room = room;
    }

    SWAPT(room->containers, dest_room->containers, t);
}

int World_AddEntity(world_p world, struct entity_s *entity)
{
    RB_InsertIgnore(&entity->id, entity, world->entity_tree);
    return 1;
}


int World_DeleteEntity(world_p world, struct entity_s *entity)
{
    RB_Delete(world->entity_tree, RB_SearchNode(&entity->id, world->entity_tree));
    return 1;
}


int World_CreateItem(world_p world, uint32_t item_id, uint32_t model_id, uint32_t world_model_id, uint16_t type, uint16_t count, const char *name)
{
    skeletal_model_p model = World_GetModelByID(world, model_id);
    if((model == NULL) || (world->items_tree == NULL))
    {
        return 0;
    }

    ss_bone_frame_p bf = (ss_bone_frame_p)malloc(sizeof(ss_bone_frame_t));
    vec3_set_zero(bf->bb_min);
    vec3_set_zero(bf->bb_max);
    vec3_set_zero(bf->centre);
    vec3_set_zero(bf->pos);
    bf->animations.anim_flags = 0x0000;
    bf->animations.frame_time = 0.0;
    bf->animations.period = 1.0 / 30.0;
    bf->animations.next_state = 0;
    bf->animations.lerp = 0.0;
    bf->animations.current_animation = 0;
    bf->animations.current_frame = 0;
    bf->animations.next_animation = 0;
    bf->animations.next_frame = 0;

    bf->animations.next = NULL;
    bf->animations.onFrame = NULL;
    bf->animations.model = model;
    bf->bone_tag_count = model->mesh_count;
    bf->bone_tags = (ss_bone_tag_p)malloc(bf->bone_tag_count * sizeof(ss_bone_tag_t));
    for(uint16_t j=0;j<bf->bone_tag_count;j++)
    {
        bf->bone_tags[j].flag = bf->animations.model->mesh_tree[j].flag;
        bf->bone_tags[j].mesh_base = bf->animations.model->mesh_tree[j].mesh_base;
        bf->bone_tags[j].mesh_skin = bf->animations.model->mesh_tree[j].mesh_skin;
        bf->bone_tags[j].mesh_slot = NULL;

        vec3_copy(bf->bone_tags[j].offset, bf->animations.model->mesh_tree[j].offset);
        vec4_set_zero(bf->bone_tags[j].qrotate);
        Mat4_E_macro(bf->bone_tags[j].transform);
        Mat4_E_macro(bf->bone_tags[j].full_transform);
    }

    base_item_p item = (base_item_p)malloc(sizeof(base_item_t));
    item->id = item_id;
    item->world_model_id = world_model_id;
    item->type = type;
    item->count = count;
    item->name[0] = 0;
    if(name)
    {
        strncpy(item->name, name, 64);
    }
    item->bf = bf;

    RB_InsertReplace(&item->id, item, world->items_tree);

    return 1;
}


int World_DeleteItem(world_p world, uint32_t item_id)
{
    RB_Delete(world->items_tree, RB_SearchNode(&item_id, world->items_tree));
    return 1;
}


struct skeletal_model_s* World_GetModelByID(world_p w, uint32_t id)
{
    long int i, min, max;

    min = 0;
    max = w->skeletal_model_count - 1;
    if(w->skeletal_models[min].id == id)
    {
        return w->skeletal_models + min;
    }
    if(w->skeletal_models[max].id == id)
    {
        return w->skeletal_models + max;
    }
    do
    {
        i = (min + max) / 2;
        if(w->skeletal_models[i].id == id)
        {
            return w->skeletal_models + i;
        }

        if(w->skeletal_models[i].id < id)
        {
            min = i;
        }
        else
        {
            max = i;
        }
    }
    while(min < max - 1);

    return NULL;
}


/*
 * find sprite by ID.
 * not a binary search - sprites may be not sorted by ID
 */
struct sprite_s* World_GetSpriteByID(unsigned int ID, world_p world)
{
    sprite_p sp = world->sprites;
    for(uint32_t i=0;i<world->sprites_count;i++,sp++)
    {
        if(sp->id == ID)
        {
            return sp;
        }
    }

    return NULL;
}


/*
 * Check for join portals existing
 */
int Room_IsJoined(room_p r1, room_p r2)
{
    portal_p p = r1->portals;
    for(uint16_t i=0;i<r1->portal_count;i++,p++)
    {
        if(p->dest_room->id == r2->id)
        {
            return 1;
        }
    }

    p = r2->portals;
    for(uint16_t i=0;i<r2->portal_count;i++,p++)
    {
        if(p->dest_room->id == r1->id)
        {
            return 1;
        }
    }

    return 0;
}

void Room_BuildNearRoomsList(room_p room)
{
    room->near_room_list_size = 0;

    portal_p p = room->portals;
    for(uint16_t i=0;i<room->portal_count;i++,p++)
    {
        Room_AddToNearRoomsList(room, p->dest_room);
    }

    uint16_t nc1 = room->near_room_list_size;

    for(uint16_t i=0;i<nc1;i++)
    {
        room_p r = room->near_room_list[i];
        p = r->portals;
        for(uint16_t j=0;j<r->portal_count;j++,p++)
        {
            Room_AddToNearRoomsList(room, p->dest_room);
        }
    }
}
