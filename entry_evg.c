#include "oogabooga/oogabooga.c"

//: constants
#define MAX_ENTITY_COUNT 4096
#define MAX_VERT 8
#define ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))

#define clamp_bottom(a, b) max(a, b)
#define clamp_top(a, b) min(a, b)

Gfx_Font *font;
const u32 font_height = 64;
const float32 font_padding = (float32)font_height / 10.0f;

const float32 spriteSheetWidth = 240.0;
const s32 tile_width = 16;

bool debug_render;

float screen_width = 240.0;
float screen_height = 135.0;

float exp_error_flash_alpha = 0;
float exp_error_flash_alpha_target = 0;
float camera_trauma = 0;
Vector2 camera_pos = {0};
float max_cam_shake_translate = 3.0f;
float max_cam_shake_rotate = 4.0f;

float64 delta_t;

//: math
inline float v2_dist(Vector2 a, Vector2 b) { return v2_length(v2_sub(a, b)); }

float v2_det(Vector2 a, Vector2 b) { return (a.x * b.y - b.x * a.y); }

float v2_angle(Vector2 a, Vector2 b) {
    float uDotV = v2_dot(a, b);
    float uDetV = v2_det(a, b);
    float angle = to_degrees(atan2(uDetV, uDotV));
    return angle;
}

Vector2 v2_proj(Vector2 a, Vector2 b) {
    float slope = v2_dot(a, b) / pow(v2_length(b), 2);
    return v2_mulf(a, slope);
}

float sin_breathe(float time, float rate) { return (sin(time * rate) + 1.0) / 2.0; }

bool almost_equals(float a, float b, float epsilon) { return fabs(a - b) <= epsilon; }

inline int extract_sign(float a) { return a == 0 ? 0 : (a < 0 ? -1 : 1); }

bool get_random_bool() { return get_random_int_in_range(0, 1); }

int get_random_sign() { return (get_random_int_in_range(0, 1) == 0 ? -1 : 1); }

// 0.2 means it has a 20% chance of returning true
bool pct_chance(float pct) { return get_random_float32_in_range(0, 1) < pct; }

float float_alpha(float x, float min, float max) {
    float res = (x - min) / (max - min);
    res = clamp(res, 0.0, 1.0);
    return res;
}

void camera_shake(float amount) { camera_trauma += amount; }

//: layer
typedef enum Layer {
    layer_stage_bg = 0,
    layer_stage_fg = 5,
    layer_world = 10,
    layer_view = 15,
    layer_entity = 20,
    layer_en_debug = 25,
    layer_ui_bg = 30,
    layer_ui_fg = 35,
    layer_text = 40,
    layer_cursor = 50,
} Layer;

//: sprite
typedef struct Sprite {
    Gfx_Image *image;
} Sprite;

typedef enum SpriteID {
    SPRITE_nil,
    SPRITE_player,
    SPRITE_monster,
    SPRITE_planet,
    SPRITE_experience,
    SPRITE_MAX,
} SpriteID;

Sprite sprites[SPRITE_MAX];

Sprite *get_sprite(SpriteID id) {
    if (id >= 0 && id < SPRITE_MAX) {
        Sprite *sprite = &sprites[id];
        if (sprite->image) {
            return sprite;
        } else {
            return &sprites[0];
        }
    }
    return &sprites[0];
}

Vector2 get_sprite_size(Sprite *sprite) { return (Vector2){sprite->image->width, sprite->image->height}; }

//: bar
typedef struct Bar {
    float64 max;
    float64 current;
    float64 rate;
} Bar;

//: ux state
typedef enum UXState {
    UX_nil,
    UX_default,
    UX_win,
    UX_lose,
} UXState;

//: entity
typedef enum EntityArchetype {
    ARCH_nil = 0,
    ARCH_player,
    ARCH_monster,
    ARCH_planet,
    ARCH_terrain,
    ARCH_weapon,
    ARCH_pickup,
    ARCH_MAX,
} EntityArchetype;

typedef enum Collider {
    COLL_nil = 0,
    COLL_point,
    COLL_line,
    COLL_rect,
    COLL_circ,
    COLL_polygon,
} Collider;

typedef struct Entity {
    bool is_valid;
    EntityArchetype arch;
    Vector4 color;
    bool is_line;
    bool is_sprite;
    bool is_attached_to_player;
    bool is_thrusting;
    SpriteID sprite_id;
    float end_time;
    Collider collider;
    // vertices are in entity space
    Vector2 vertices[4];
    Vector2 size;
    Vector2 move_vec;
    Vector2 input_axis;
    float move_speed;
    float64 mass;
    Vector2 center_mass;
    bool is_static;
    // anchored bottom left of "square"
    Vector2 pos;
    Vector2 velocity;
    Vector2 last_momentum;
    Vector2 momentum;
    float orientation;
    float angular_momentum;
    float angular_velocity;
    Bar energy;
} Entity;

void entity_apply_defaults(Entity *en) {}

Vector2 get_entity_midpoint(Entity *en) { return v2(en->pos.x + en->size.x / 2.0, en->pos.y + en->size.y / 2.0); }

Vector2 get_line_endpoint(Entity *en) { return v2_add(en->pos, en->size); }

//: collision

bool point_point_collision(Entity *en_p1, Entity *en_p2) {
    bool collision_detected = false;
    if (en_p1->pos.x == en_p2->pos.x && en_p1->pos.y == en_p2->pos.y) {
        collision_detected = true;
    }
    return collision_detected;
}

bool point_circle_collision(Entity *en_p, Entity *en_c) {
    bool collision_detected = false;
    float dist = v2_dist(en_p->pos, get_entity_midpoint(en_c));
    if (dist <= en_c->size.x) {
        collision_detected = true;
    }
    return collision_detected;
}
bool circle_point_collision(Entity *en_c, Entity *en_p) { return point_circle_collision(en_p, en_c); }

bool circle_circle_collision(Entity *en_c1, Entity *en_c2) {
    bool collision_detected = false;
    float dist = v2_dist(get_entity_midpoint(en_c1), get_entity_midpoint(en_c2));
    float combRad = (en_c1->size.x + en_c2->size.y) / 2.0f;
    if (dist <= combRad) {
        collision_detected = true;
    }
    return collision_detected;
}

bool point_rectangle_collision(Entity *en_p, Entity *en_r) {
    bool collision_detected = false;
    if (en_p->pos.x >= en_r->pos.x && en_p->pos.x <= en_r->pos.x + en_r->size.x && en_p->pos.y >= en_r->pos.y &&
        en_p->pos.y <= en_r->pos.y + en_r->size.y) {
        collision_detected = true;
    }
    return collision_detected;
}
bool rectangle_point_collision(Entity *en_r, Entity *en_p) { return point_rectangle_collision(en_p, en_r); }

bool rectangle_rectangle_collision(Entity *en_r1, Entity *en_r2) {
    bool collision_detected = false;
    if (en_r1->pos.x < en_r2->pos.x + en_r2->size.x && en_r1->pos.x + en_r1->size.x > en_r2->pos.x &&
        en_r1->pos.y < en_r2->pos.y + en_r2->size.y && en_r1->pos.y + en_r1->size.y > en_r2->pos.y) {
        collision_detected = true;
    }
    return collision_detected;
}

bool circle_rectangle_collision(Entity *en_c, Entity *en_r) {
    bool collision_detected = false;
    float testX = get_entity_midpoint(en_c).x;
    float testY = get_entity_midpoint(en_c).y;
    if (get_entity_midpoint(en_c).x < en_r->pos.x)
        testX = en_r->pos.x; // left edge
    else if (get_entity_midpoint(en_c).x > en_r->pos.x + en_r->size.x)
        testX = en_r->pos.x + en_r->size.x; // right edge

    if (get_entity_midpoint(en_c).y < en_r->pos.y)
        testY = en_r->pos.y; // bottom edge
    else if (get_entity_midpoint(en_c).y > en_r->pos.y + en_r->size.y)
        testY = en_r->pos.y + en_r->size.y; // top edge

    float distX = get_entity_midpoint(en_c).x - testX;
    float distY = get_entity_midpoint(en_c).y - testY;
    float distance = sqrt((distX * distX) + (distY * distY));

    if (distance <= en_c->size.x / 2.0f) {
        collision_detected = true;
    }
    return collision_detected;
}
bool rectangle_circle_collision(Entity *en_r, Entity *en_c) { return circle_rectangle_collision(en_c, en_r); }

bool line_point_collision(Entity *en_l, Entity *en_p) {
    bool collision_detected = false;
    float dist1 = v2_dist(en_l->pos, en_p->pos);
    float dist2 = v2_dist(get_line_endpoint(en_l), en_p->pos);
    if (almost_equals(dist1 + dist2, v2_length(en_l->size), 0.05)) {
        collision_detected = true;
    }
    return collision_detected;
}
bool point_line_collision(Entity *en_p, Entity *en_l) { return line_point_collision(en_l, en_p); }

bool line_circle_collision(Entity *en_l, Entity *en_c) {
    bool collision_detected = false;

    // endpoint checks
    collision_detected = point_circle_collision(en_l, en_c);

    en_l->pos = v2_add(en_l->pos, en_l->size);
    collision_detected = point_circle_collision(en_l, en_c) ? true : collision_detected;
    en_l->pos = v2_sub(en_l->pos, en_l->size);

    // stop early if possible
    if (collision_detected) {
        return collision_detected;
    }

    Vector2 closest_point = v2_proj(get_line_endpoint(en_l), get_entity_midpoint(en_c));
    Vector2 temp_pos = en_c->pos;
    en_c->pos = closest_point;
    collision_detected = point_line_collision(en_c, en_l);
    en_c->pos = temp_pos;
    // bail early again
    if (!collision_detected) {
        return collision_detected;
    }

    temp_pos = en_l->pos;
    en_l->pos = closest_point;
    collision_detected = point_circle_collision(en_l, en_c);
    en_l->pos = temp_pos;

    return collision_detected;
}
bool circle_line_collision(Entity *en_c, Entity *en_l) { return line_circle_collision(en_l, en_c); }

bool line_line_collision(Entity *en_l1, Entity *en_l2) {
    bool collision_detected = false;

    float uA = ((get_line_endpoint(en_l2).x - en_l2->pos.x) * (en_l1->pos.y - en_l2->pos.y) -
                (get_line_endpoint(en_l2).y - en_l2->pos.y) * (en_l1->pos.x - en_l2->pos.x)) /
               ((get_line_endpoint(en_l2).y - en_l2->pos.y) * (get_line_endpoint(en_l1).x - en_l1->pos.x) -
                (get_line_endpoint(en_l2).x - en_l2->pos.x) * (get_line_endpoint(en_l1).y - en_l1->pos.y));
    float uB = ((get_line_endpoint(en_l1).x - en_l1->pos.x) * (en_l1->pos.y - en_l2->pos.y) -
                (get_line_endpoint(en_l1).y - en_l1->pos.y) * (en_l1->pos.x - en_l2->pos.x)) /
               ((get_line_endpoint(en_l2).y - en_l2->pos.y) * (get_line_endpoint(en_l1).x - en_l1->pos.x) -
                (get_line_endpoint(en_l2).x - en_l2->pos.x) * (get_line_endpoint(en_l1).y - en_l1->pos.y));
    if (uA >= 0 && uA <= 1 && uB >= 0 && uB <= 1) {
        // point of intersection in 2d with bool found value as z
        float intersectionX = en_l1->pos.x + (uA * (get_line_endpoint(en_l1).x - en_l1->pos.x));
        float intersectionY = en_l1->pos.y + (uA * (get_line_endpoint(en_l1).y - en_l1->pos.y));
        collision_detected = true;
    }
    return collision_detected;
}

bool line_rectangle_collision(Entity *en_l, Entity *en_r) {
    bool collision_detected = false;
    Vector2 temp_pos = en_r->pos;
    Vector2 temp_size = en_r->size;

    // left
    en_r->size = v2(0, temp_size.y);
    collision_detected = line_line_collision(en_l, en_r) ? true : collision_detected;

    // top
    en_r->pos = v2_add(temp_pos, v2(0, temp_size.y));
    en_r->size = v2(temp_size.x, 0);
    collision_detected = line_line_collision(en_l, en_r) ? true : collision_detected;

    // right
    en_r->pos = v2_add(temp_pos, temp_size);
    en_r->size = v2(0, -temp_size.y);
    collision_detected = line_line_collision(en_l, en_r) ? true : collision_detected;

    // bottom
    en_r->pos = v2_add(temp_pos, v2(temp_size.x, 0));
    en_r->size = v2(-temp_size.x, 0);
    collision_detected = line_line_collision(en_l, en_r) ? true : collision_detected;

    en_r->pos = temp_pos;
    en_r->size = temp_size;

    return collision_detected;
}
bool rectangle_line_collision(Entity *en_r, Entity *en_l) { return line_rectangle_collision(en_l, en_r); }

bool polygon_point_collision(Entity *en_y, Entity *en_p) {
    bool collision_detected = false;
    int next = 0;
    // hardcoding as we only support oriented rect colliders for now
    for (int i = 0; i < 4; i++) {
        next = i + 1;
        if (next == 4) {
            next = 0;
        }
        Vector2 vc = en_y->vertices[i];
        Vector2 vn = en_y->vertices[next];

        if (((vc.y >= en_p->pos.y && vn.y < en_p->pos.y) || (vc.y < en_p->pos.y && vn.y >= en_p->pos.y)) &&
            (en_p->pos.x < (vn.x - vc.x) * (en_p->pos.y - vc.y) / (vn.y - vc.y) + vc.x)) {
            collision_detected = !collision_detected;
        }
    }
    return collision_detected;
}
bool point_polygon_collision(Entity *en_p, Entity *en_y) { return polygon_point_collision(en_y, en_p); }

bool polygon_circle_collision(Entity *en_y, Entity *en_c) {
    bool collision_detected = false;

    Vector2 temp_pos = en_y->pos;
    Vector2 temp_size = en_y->size;

    int next = 0;
    for (int i = 0; i < 4; i++) {
        next = i + 1;
        if (next == 4) {
            next = 0;
        }
        Vector2 vc = en_y->vertices[i];
        Vector2 vn = en_y->vertices[next];

        en_y->pos = vc;
        en_y->size = v2_sub(vn, vc);
        collision_detected = line_circle_collision(en_y, en_c);

        if (collision_detected) {
            en_y->pos = temp_pos;
            en_y->size = temp_size;
            return collision_detected;
        }
    }

    en_y->pos = temp_pos;
    en_y->size = temp_size;

    return collision_detected;
}
bool circle_polygon_collision(Entity *en_c, Entity *en_y) { return polygon_circle_collision(en_y, en_c); }

bool polygon_rectangle_collision(Entity *en_y, Entity *en_r) {
    bool collision_detected = false;

    Vector2 temp_pos = en_y->pos;
    Vector2 temp_size = en_y->size;

    int next = 0;
    for (int i = 0; i < 4; i++) {
        next = i + 1;
        if (next == 4) {
            next = 0;
        }
        Vector2 vc = en_y->vertices[i];
        Vector2 vn = en_y->vertices[next];

        en_y->pos = vc;
        en_y->size = v2_sub(vn, vc);
        collision_detected = line_rectangle_collision(en_y, en_r);

        if (collision_detected) {
            en_y->pos = temp_pos;
            en_y->size = temp_size;
            return collision_detected;
        }
    }

    en_y->pos = temp_pos;
    en_y->size = temp_size;

    return collision_detected;
}
bool rectangle_polygon_collision(Entity *en_r, Entity *en_y) { return polygon_rectangle_collision(en_y, en_r); }

bool polygon_line_collision(Entity *en_y, Entity *en_l) {
    bool collision_detected = false;

    Vector2 temp_pos = en_y->pos;
    Vector2 temp_size = en_y->size;

    int next = 0;
    for (int i = 0; i < 4; i++) {
        next = i + 1;
        if (next == 4) {
            next = 0;
        }
        Vector2 vc = en_y->vertices[i];
        Vector2 vn = en_y->vertices[next];

        en_y->pos = vc;
        en_y->size = v2_sub(vn, vc);
        collision_detected = line_line_collision(en_y, en_l);

        if (collision_detected) {
            en_y->pos = temp_pos;
            en_y->size = temp_size;
            return collision_detected;
        }
    }

    en_y->pos = temp_pos;
    en_y->size = temp_size;

    return collision_detected;
}
bool line_polygon_collision(Entity *en_l, Entity *en_y) { return polygon_line_collision(en_y, en_l); }

bool polygon_polygon_collision(Entity *en_y1, Entity *en_y2) {
    bool collision_detected = false;

    Vector2 temp_pos = en_y1->pos;
    Vector2 temp_size = en_y1->size;

    int next = 0;
    for (int i = 0; i < 4; i++) {
        next = i + 1;
        if (next == 4) {
            next = 0;
        }
        Vector2 vc = en_y1->vertices[i];
        Vector2 vn = en_y1->vertices[next];

        en_y1->pos = vc;
        en_y1->size = v2_sub(vn, vc);
        collision_detected = line_polygon_collision(en_y1, en_y2);

        if (collision_detected) {
            en_y1->pos = temp_pos;
            en_y1->size = temp_size;
            return collision_detected;
        }
    }

    en_y1->pos = temp_pos;
    en_y1->size = temp_size;

    return collision_detected;
}

bool check_entity_collision(Entity *en_1, Entity *en_2) {
    bool collision_detected = false;
    if (en_1->is_valid && en_2->is_valid) {
        if (en_1->collider == COLL_rect && en_2->collider == COLL_rect) {
            collision_detected = rectangle_rectangle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_line && en_2->collider == COLL_line) {
            collision_detected = line_line_collision(en_1, en_2);
        } else if (en_1->collider == COLL_line && en_2->collider == COLL_rect) {
            collision_detected = line_rectangle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_rect && en_2->collider == COLL_line) {
            collision_detected = rectangle_line_collision(en_1, en_2);
        } else if (en_1->collider == COLL_point && en_2->collider == COLL_rect) {
            collision_detected = point_rectangle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_rect && en_2->collider == COLL_point) {
            collision_detected = rectangle_point_collision(en_1, en_2);
        } else if (en_1->collider == COLL_circ && en_2->collider == COLL_circ) {
            collision_detected = circle_circle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_point && en_2->collider == COLL_point) {
            collision_detected = point_point_collision(en_1, en_2);
        } else if (en_1->collider == COLL_point && en_2->collider == COLL_circ) {
            collision_detected = point_circle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_circ && en_2->collider == COLL_point) {
            collision_detected = circle_point_collision(en_1, en_2);
        } else if (en_1->collider == COLL_rect && en_2->collider == COLL_circ) {
            collision_detected = rectangle_circle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_circ && en_2->collider == COLL_rect) {
            collision_detected = circle_rectangle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_point && en_2->collider == COLL_line) {
            collision_detected = point_line_collision(en_1, en_2);
        } else if (en_1->collider == COLL_line && en_2->collider == COLL_point) {
            collision_detected = line_point_collision(en_1, en_2);
        } else if (en_1->collider == COLL_point && en_2->collider == COLL_point) {
            collision_detected = point_point_collision(en_1, en_2);
        } else if (en_1->collider == COLL_line && en_2->collider == COLL_circ) {
            collision_detected = line_circle_collision(en_1, en_2);
        } else if (en_1->collider == COLL_circ && en_2->collider == COLL_line) {
            collision_detected = circle_line_collision(en_1, en_2);
        }
    }

    return collision_detected;
}

bool check_entity_will_collide(Entity *en_1, Entity *en_2) {
    bool collision_detected = false;

    // check next frame based on current move vecs
    en_1->pos = v2_add(en_1->pos, v2_mulf(en_1->velocity, delta_t));
    en_2->pos = v2_add(en_2->pos, v2_mulf(en_2->velocity, delta_t));
    collision_detected = check_entity_collision(en_1, en_2);

    en_1->pos = v2_sub(en_1->pos, v2_mulf(en_1->velocity, delta_t));
    en_2->pos = v2_sub(en_2->pos, v2_mulf(en_2->velocity, delta_t));

    return collision_detected;
}

void solid_entity_collision(Entity *en_1, Entity *en_2) {
    Collider temp_coll1 = en_1->collider;
    Collider temp_coll2 = en_2->collider;

    if (en_1->collider == COLL_rect && en_1->orientation != 0) {
        en_1->collider = COLL_polygon;
        for (int i = 0; i < 4; i++) {
            Matrix4 xform = m4_scalar(1.0);
            xform = m4_translate(xform, v3(en_1->pos.x, en_1->pos.y, 0));
            xform = m4_translate(xform, v3(en_1->center_mass.x, en_1->center_mass.y, 0));
            xform = m4_rotate(xform, v3(0, 0, 1), en_1->orientation);
            xform = m4_translate(xform, v3(-en_1->center_mass.x, -en_1->center_mass.y, 0));
            // extract new vertex
            en_1->vertices[i] = v2(xform.m[3][1], xform.m[3][2]);
        }
    }
    if (en_2->collider == COLL_rect && en_2->orientation != 0) {
        en_2->collider = COLL_polygon;
        for (int i = 0; i < 4; i++) {
            Matrix4 xform = m4_scalar(2.0);
            xform = m4_translate(xform, v3(en_2->pos.x, en_2->pos.y, 0));
            xform = m4_translate(xform, v3(en_2->center_mass.x, en_2->center_mass.y, 0));
            xform = m4_rotate(xform, v3(0, 0, 2), en_2->orientation);
            xform = m4_translate(xform, v3(-en_2->center_mass.x, -en_2->center_mass.y, 0));
            // extract new vertex
            en_2->vertices[i] = v2(xform.m[3][1], xform.m[3][2]);
        }
    }

    Vector2 en_to_en_vec = v2_sub(get_entity_midpoint(en_2), get_entity_midpoint(en_1));
    Vector2 down_vec = v2_normalize(en_to_en_vec);
    float g_mag = 6.67 * pow(10.0f, -11.0f) * en_2->mass * en_1->mass / pow(v2_length(en_to_en_vec), 2.0f);
    g_mag = clamp_top(g_mag, 9.8 * 10 * 20);
    Vector2 g_force = v2_mulf(v2_normalize(en_to_en_vec), g_mag);

    if (check_entity_collision(en_1, en_2)) {
        en_1->momentum = v2_add(en_1->momentum, v2_mulf(g_force, -1.0f * delta_t));
    } else if (check_entity_will_collide(en_1, en_2)) {
        Vector2 impact_force =
            v2_mulf(en_1->velocity,
                    v2_dot(v2_normalize(en_to_en_vec), v2_normalize(en_1->velocity)) * -1.0f * en_1->mass / delta_t);
        en_1->momentum = v2_add(en_1->momentum, v2_mulf(impact_force, delta_t));
    } else {
        en_1->momentum = v2_add(en_1->momentum, v2_mulf(g_force, delta_t));
    }
    en_1->velocity = v2_divf(en_1->momentum, en_1->mass);

    en_1->collider = temp_coll1;
    en_2->collider = temp_coll2;
}

//: world
typedef struct World {
    Entity entities[MAX_ENTITY_COUNT];
    UXState ux_state;
    float64 time_elapsed;
} World;
World *world = 0;

//: serialisation
bool world_save_to_disk() { return os_write_entire_file_s(STR("world"), (string){sizeof(World), (u8 *)world}); }
bool world_attempt_load_from_disk() {
    string result = {0};
    bool succ = os_read_entire_file_s(STR("world"), &result, temp_allocator);
    if (!succ) {
        log_error("Failed to load world.");
        return false;
    }

    if (result.count != sizeof(World)) {
        log_error("world size different to one on disk.");
        return false;
    }

    memcpy(world, result.data, result.count);
    return true;
}

//: frame
typedef struct WorldFrame {
    Entity *selected_entity;
    Matrix4 world_proj;
    Matrix4 world_view;
    Entity *player;
    Entity *planet;
} WorldFrame;
WorldFrame world_frame;

Entity *get_player() { return world_frame.player; }
Entity *get_planet() { return world_frame.planet; }

//: setup
Entity *entity_create() {
    Entity *entity_found = 0;
    for (int i = 0; i < MAX_ENTITY_COUNT; i++) {
        Entity *existing_entity = &world->entities[i];
        if (!existing_entity->is_valid) {
            entity_found = existing_entity;
            break;
        }
    }
    assert(entity_found, "No more free entities!");
    entity_found->is_valid = true;
    return entity_found;
}

void entity_destroy(Entity *entity) { memset(entity, 0, sizeof(Entity)); }

void set_rectangle_collider(Entity *en) {
    en->collider = COLL_rect;
    en->vertices[0] = v2(0, 0);
    en->vertices[1] = v2(0, en->size.y);
    en->vertices[2] = en->size;
    en->vertices[3] = v2(en->size.x, 0);
}

void setup_player(Entity *en) {
    en->arch = ARCH_player;
    en->is_sprite = true;
    en->sprite_id = SPRITE_player;
    Sprite *sprite = get_sprite(en->sprite_id);
    en->size = get_sprite_size(sprite);
    en->size = v2(68, 68);
    // set_rectangle_collider(en);
    en->collider = COLL_circ;
    en->color = COLOR_WHITE;
    en->move_speed = 150.0;
    en->mass = 5.9722;
    en->center_mass = get_entity_midpoint(en);
    en->energy.max = 50;
    en->energy.current = en->energy.max;
}

void setup_planet(Entity *en) {
    en->arch = ARCH_planet;
    en->is_sprite = true;
    en->collider = COLL_circ;
    en->is_static = true;
    en->color = COLOR_WHITE;
    en->sprite_id = SPRITE_planet;
    Sprite *sprite = get_sprite(en->sprite_id);
    en->size = v2(256, 256);
    en->mass = 5.9722 * pow(10, 20);
    en->center_mass = get_entity_midpoint(en);
}

void setup_moon(Entity *en) {
    en->arch = ARCH_planet;
    en->is_sprite = true;
    en->collider = COLL_circ;
    en->is_static = true;
    en->color = COLOR_WHITE;
    en->sprite_id = SPRITE_planet;
    Sprite *sprite = get_sprite(en->sprite_id);
    en->size = v2(128, 128);
    en->mass = 5.9722 * pow(10, 14);
    en->center_mass = get_entity_midpoint(en);
}

void setup_world() {

    world->ux_state = UX_default;
    world->time_elapsed = 0;

    Entity *planet_en = entity_create();
    setup_planet(planet_en);
    planet_en->pos = v2(0, 0);

    // Entity *moon_en = entity_create();
    // setup_moon(moon_en);
    // moon_en->pos = v2(0, 1000);

    // Entity *planet_en1 = entity_create();
    // setup_planet(planet_en1);
    // planet_en1->pos = v2(500, 50);

    Entity *player_en = entity_create();
    setup_player(player_en);
    player_en->pos =
        v2(planet_en->size.x / 2.0f - player_en->size.x / 2.0f, planet_en->size.y + 2.0 * player_en->size.y);
}

void teardown_world() {
    for (int i = 0; i < MAX_ENTITY_COUNT; i++) {
        Entity *en = &world->entities[i];
        entity_destroy(en);
    }
}

World *world_create() { return world; }

void render_sprite_entity(Entity *en) {
    if (en->is_valid) {
        Sprite *sprite = get_sprite(en->sprite_id);
        Matrix4 xform = m4_scalar(1.0);
        xform = m4_translate(xform, v3(en->pos.x, en->pos.y, 0));
        xform = m4_translate(xform, v3(en->center_mass.x, en->center_mass.y, 0));
        xform = m4_rotate(xform, v3(0, 0, 1), en->orientation);
        xform = m4_translate(xform, v3(-en->center_mass.x, -en->center_mass.y, 0));
        draw_image_xform(sprite->image, xform, en->size, en->color);
        if (debug_render) {
            if (en->collider == COLL_rect) {
                draw_rect_xform(xform, en->size, v4(en->color.r, en->color.g, en->color.b, 0.3));
            }
            if (en->collider == COLL_circ) {
                draw_circle_xform(xform, en->size, v4(en->color.r, en->color.g, en->color.b, 0.3));
            }
        }
    }
}

void render_rect_entity(Entity *en) {
    if (en->is_valid) {
        Matrix4 xform = m4_scalar(1.0);
        xform = m4_translate(xform, v3(en->pos.x, en->pos.y, 0));
        draw_rect_xform(xform, en->size, en->color);
    }
}

void render_line_entity(Entity *en) {
    if (en->is_valid) {
        Vector2 endpoint = get_line_endpoint(en);
        draw_line(en->pos, endpoint, en->size.y, en->color);
    }
}

//: coordinate conversion
void set_screen_space() {
    draw_frame.camera_xform = m4_scalar(1.0);
    draw_frame.projection = m4_make_orthographic_projection(0.0, screen_width, 0.0, screen_height, -1, 10);
}
void set_world_space() {
    draw_frame.projection = world_frame.world_proj;
    draw_frame.camera_xform = world_frame.world_view;
}

Vector2 world_to_screen(Vector2 p) {
    Vector4 in_cam_space = m4_transform(draw_frame.camera_xform, v4(p.x, p.y, 0.0, 1.0));
    Vector4 in_clip_space = m4_transform(draw_frame.projection, in_cam_space);

    Vector4 ndc = {.x = in_clip_space.x / in_clip_space.w,
                   .y = in_clip_space.y / in_clip_space.w,
                   .z = in_clip_space.z / in_clip_space.w,
                   .w = in_clip_space.w};

    return v2((ndc.x + 1.0f) * 0.5f * (f32)window.width, (ndc.y + 1.0f) * 0.5f * (f32)window.height);
}

Vector2 world_size_to_screen_size(Vector2 s) {
    Vector2 origin = v2(0, 0);

    Vector2 screen_origin = world_to_screen(origin);
    Vector2 screen_size_point = world_to_screen(s);

    return v2(screen_size_point.x - screen_origin.x, screen_size_point.y - screen_origin.y);
}

Vector2 screen_to_world(Vector2 screen_pos) {
    Matrix4 proj = draw_frame.projection;
    Matrix4 view = draw_frame.camera_xform;
    float window_w = window.width;
    float window_h = window.height;

    // Normalize the mouse coordinates
    float ndc_x = (screen_pos.x / (window_w * 0.5f)) - 1.0f;
    float ndc_y = (screen_pos.y / (window_h * 0.5f)) - 1.0f;

    // Transform to world coordinates
    Vector4 world_pos = v4(ndc_x, ndc_y, 0, 1);
    world_pos = m4_transform(m4_inverse(proj), world_pos);
    world_pos = m4_transform(view, world_pos);
    // log("%f, %f", world_pos.x, world_pos.y);

    // Return as 2D vector
    return (Vector2){world_pos.x, world_pos.y};
}

int world_pos_to_tile_pos(float world_pos) { return roundf(world_pos / (float)tile_width); }

float tile_pos_to_world_pos(int tile_pos) { return ((float)tile_pos * (float)tile_width); }

Vector2 round_v2_to_tile(Vector2 world_pos) {
    world_pos.x = tile_pos_to_world_pos(world_pos_to_tile_pos(world_pos.x));
    world_pos.y = tile_pos_to_world_pos(world_pos_to_tile_pos(world_pos.y));
    return world_pos;
}

Vector2 get_mouse_pos_in_ndc() {
    float mouse_x = input_frame.mouse_x;
    float mouse_y = input_frame.mouse_y;
    Matrix4 proj = draw_frame.projection;
    Matrix4 view = draw_frame.camera_xform;
    float window_w = window.width;
    float window_h = window.height;

    // Normalize the mouse coordinates
    float ndc_x = (mouse_x / (window_w * 0.5f)) - 1.0f;
    float ndc_y = (mouse_y / (window_h * 0.5f)) - 1.0f;

    return (Vector2){ndc_x, ndc_y};
}

Draw_Quad ndc_quad_to_screen_quad(Draw_Quad ndc_quad) {
    // NOTE: we're assuming these are the screen space matricies.
    Matrix4 proj = draw_frame.projection;
    Matrix4 view = draw_frame.camera_xform;

    Matrix4 ndc_to_screen_space = m4_scalar(1.0);
    ndc_to_screen_space = m4_mul(ndc_to_screen_space, m4_inverse(proj));
    ndc_to_screen_space = m4_mul(ndc_to_screen_space, view);

    ndc_quad.bottom_left = m4_transform(ndc_to_screen_space, v4(v2_expand(ndc_quad.bottom_left), 0, 1)).xy;
    ndc_quad.bottom_right = m4_transform(ndc_to_screen_space, v4(v2_expand(ndc_quad.bottom_right), 0, 1)).xy;
    ndc_quad.top_left = m4_transform(ndc_to_screen_space, v4(v2_expand(ndc_quad.top_left), 0, 1)).xy;
    ndc_quad.top_right = m4_transform(ndc_to_screen_space, v4(v2_expand(ndc_quad.top_right), 0, 1)).xy;

    return ndc_quad;
}

//: animate
inline float64 now() { return world->time_elapsed; }
inline float64 app_now() { return os_get_elapsed_seconds(); }

float alpha_from_end_time(float64 end_time, float length) { return float_alpha(now(), end_time - length, end_time); }

bool has_reached_end_time(float64 end_time) { return now() > end_time; }

bool animate_f32_to_target(float *value, float target, float rate) {
    *value += (target - *value) * (1.0 - pow(2.0f, -rate * delta_t));
    if (almost_equals(*value, target, 0.001f)) {
        *value = target;
        return true; // reached
    }
    return false;
}

void animate_v2_to_target(Vector2 *value, Vector2 target, float rate) {
    animate_f32_to_target(&(value->x), target.x, rate);
    animate_f32_to_target(&(value->y), target.y, rate);
}

// :particle system
typedef enum ParticleFlags {
    PARTICLE_FLAGS_valid = (1 << 0),
    PARTICLE_FLAGS_physics = (1 << 1),
    PARTICLE_FLAGS_friction = (1 << 2),
    PARTICLE_FLAGS_fade_out_with_velocity = (1 << 3),
    // PARTICLE_FLAGS_gravity = (1<<3),
    // PARTICLE_FLAGS_bounce = (1<<4),
} ParticleFlags;
typedef struct Particle {
    ParticleFlags flags;
    Vector4 col;
    Vector2 pos;
    Vector2 velocity;
    Vector2 acceleration;
    float friction;
    float64 end_time;
    float fade_out_vel_range;
} Particle;
Particle particles[2048] = {0};
int particle_cursor = 0;

Particle *particle_new() {
    Particle *p = &particles[particle_cursor];
    particle_cursor += 1;
    if (particle_cursor >= ARRAY_COUNT(particles)) {
        particle_cursor = 0;
    }
    if (p->flags & PARTICLE_FLAGS_valid) {
        log_warning("too many particles, overwriting existing");
    }
    p->flags |= PARTICLE_FLAGS_valid;
    return p;
}
void particle_clear(Particle *p) { memset(p, 0, sizeof(Particle)); }
void particle_update() {
    for (int i = 0; i < ARRAY_COUNT(particles); i++) {
        Particle *p = &particles[i];
        if (!(p->flags & PARTICLE_FLAGS_valid)) {
            continue;
        }

        if (p->end_time && has_reached_end_time(p->end_time)) {
            particle_clear(p);
            continue;
        }

        if (p->flags & PARTICLE_FLAGS_fade_out_with_velocity && v2_length(p->velocity) < 0.01) {
            particle_clear(p);
        }

        if (p->flags & PARTICLE_FLAGS_physics) {
            if (p->flags & PARTICLE_FLAGS_friction) {
                p->acceleration = v2_sub(p->acceleration, v2_mulf(p->velocity, p->friction));
            }
            p->velocity = v2_add(p->velocity, v2_mulf(p->acceleration, delta_t));
            Vector2 next_pos = v2_add(p->pos, v2_mulf(p->velocity, delta_t));
            p->acceleration = (Vector2){0};
            p->pos = next_pos;
        }
    }
}
void particle_render() {
    for (int i = 0; i < ARRAY_COUNT(particles); i++) {
        Particle *p = &particles[i];
        if (!(p->flags & PARTICLE_FLAGS_valid)) {
            continue;
        }

        Vector4 col = p->col;
        if (p->flags & PARTICLE_FLAGS_fade_out_with_velocity) {
            col.a *= float_alpha(fabsf(v2_length(p->velocity)), 0, p->fade_out_vel_range);
        }

        draw_rect(p->pos, v2(1, 1), col);
    }
}

typedef enum ParticleKind {
    PFX_footstep,
    PFX_hit,
    // :particle
} ParticleKind;
void particle_emit(Vector2 pos, ParticleKind kind) {
    switch (kind) {
    case PFX_footstep: {
        // ...
    } break;

    case PFX_hit: {
        for (int i = 0; i < 4; i++) {
            Particle *p = particle_new();
            p->flags |= PARTICLE_FLAGS_physics | PARTICLE_FLAGS_friction | PARTICLE_FLAGS_fade_out_with_velocity;
            p->pos = pos;
            p->velocity = v2_normalize(v2(get_random_float32_in_range(-1, 1), get_random_float32_in_range(-1, 1)));
            p->velocity = v2_mulf(p->velocity, get_random_float32_in_range(20, 20));
            p->col = COLOR_RED;
            p->friction = 20.0f;
            p->fade_out_vel_range = 20.0f;
        }
    } break;
    }
}

//: entry
int entry(int argc, char **argv) {

    window.title = STR("Escape Velocity");
    window.point_width = 1280; // We need to set the scaled size if we want to
                               // handle system scaling (DPI)
    window.point_height = 720;
    window.x = 200;
    window.y = 200;
    window.clear_color = v4(0.07, 0.07, 0.07, 1);
    window.force_topmost = false;

    seed_for_random = rdtsc();

    {
        sprites[0] =
            (Sprite){.image = load_image_from_disk(fixed_string("res\\sprites\\undefined.png"), get_heap_allocator())};
        sprites[SPRITE_player] =
            (Sprite){.image = load_image_from_disk(fixed_string("res\\sprites\\player.png"), get_heap_allocator())};
        sprites[SPRITE_monster] =
            (Sprite){.image = load_image_from_disk(fixed_string("res\\sprites\\monster.png"), get_heap_allocator())};
        sprites[SPRITE_planet] =
            (Sprite){.image = load_image_from_disk(fixed_string("res\\sprites\\world.png"), get_heap_allocator())};
        sprites[SPRITE_experience] =
            (Sprite){.image = load_image_from_disk(fixed_string("res\\sprites\\sword.png"), get_heap_allocator())};

        for (SpriteID i = 0; i < SPRITE_MAX; i++) {
            Sprite *sprite = &sprites[i];
            assert(sprite->image, "Sprite was not setup properly");
        }
    }

    world = alloc(get_heap_allocator(), sizeof(World));
    memset(world, 0, sizeof(World));
    setup_world();

    debug_render = true;
    font = load_font_from_disk(STR("C:/windows/fonts/arial.ttf"), get_heap_allocator());
    assert(font, "Failed loading arial.ttf, %d", GetLastError());
    render_atlas_if_not_yet_rendered(font, 32, 'A');

    float64 seconds_counter = 0.0;
    s32 frame_count = 0;
    s32 last_fps = 0;
    float64 last_time = os_get_elapsed_seconds();
    float64 start_time = os_get_elapsed_seconds();
    Vector2 camera_pos = v2(0, 0);
    bool reset_world = false;

    //: loop
    while (!window.should_close) {
        reset_temporary_storage();

        if (reset_world) {
            teardown_world();
            setup_world();
            reset_world = false;
        }

        world_frame = (WorldFrame){0};
        float64 now = os_get_elapsed_seconds();
        delta_t = now - last_time;
// debug clamp dt for breakpoints
#if CONFIGURATION == DEBUG
        { clamp_top(delta_t, 0.017); }
#endif

        last_time = now;

        float zoom = 1.0;

        // find player
        for (int i = 0; i < MAX_ENTITY_COUNT; i++) {
            Entity *en = &world->entities[i];
            if (!en->is_valid) {
                // clean up flagged entities before these pointers get used and
                // after render
                entity_destroy(en);
            } else if (en->is_valid && en->arch == ARCH_player) {
                world_frame.player = en;
            } else if (en->is_valid && en->arch == ARCH_planet) {
                world_frame.planet = en;
            }
        }

        //: frame updating
        draw_frame.enable_z_sorting = true;
        world_frame.world_proj = m4_make_orthographic_projection(window.width * -0.5, window.width * 0.5,
                                                                 window.height * -0.5, window.height * 0.5, -1, 10);

        // :camera
        {
            camera_trauma -= delta_t;
            camera_trauma = clamp_bottom(camera_trauma, 0);
            camera_trauma = clamp_top(camera_trauma, 1);
            float cam_shake = clamp_top(pow(camera_trauma, 2), 1);

            Vector2 target_pos = get_player()->pos;
            animate_v2_to_target(&camera_pos, target_pos, v2_length(get_player()->velocity) * 0.99);

            world_frame.world_view = m4_identity();

            // randy: these might be ordered incorrectly for the camera shake.
            // Not sure.

            // translate into position
            world_frame.world_view = m4_translate(world_frame.world_view, v3(camera_pos.x, camera_pos.y, 0));

            // translational shake
            float shake_x = max_cam_shake_translate * cam_shake * get_random_float32_in_range(-1, 1);
            float shake_y = max_cam_shake_translate * cam_shake * get_random_float32_in_range(-1, 1);
            world_frame.world_view = m4_translate(world_frame.world_view, v3(shake_x, shake_y, 0));

            // rotational shake
            // float shake_rotate = max_cam_shake_rotate * cam_shake *
            // get_random_float32_in_range(-1, 1); world_frame.world_view =
            // m4_rotate_z(world_frame.world_view, shake_rotate);

            // scale the zoom
            world_frame.world_view = m4_scale(world_frame.world_view, v3(1.0 / zoom, 1.0 / zoom, 1.0));

            // log("trauma %f shake %f", camera_trauma, camera_shake);
        }

        //: input
        {
            // check exit cond first
            if (is_key_just_pressed(KEY_ESCAPE)) {
                window.should_close = true;
            }
            if (is_key_just_pressed(KEY_F11)) {
                consume_key_just_pressed(KEY_F11);
                window.fullscreen = !window.fullscreen;
            }
            if (is_key_just_pressed('R')) {
                reset_world = true;
            }

            get_player()->input_axis = v2(0, 0);
            if (world->ux_state != UX_win && world->ux_state != UX_lose) {
                if (is_key_down('A')) {
                    get_player()->input_axis.x -= 1.0;
                }
                if (is_key_down('D')) {
                    get_player()->input_axis.x += 1.0;
                }
                if (is_key_down('S')) {
                    get_player()->input_axis.y -= 1.0;
                }
                if (is_key_down('W')) {
                    get_player()->input_axis.y += 1.0;
                }
                if (is_key_down(KEY_SPACEBAR)) {
                    get_player()->is_thrusting = true;
                    // play_one_audio_clip(fixed_string("res\\sound\\burn.wav"));
                } else {
                    get_player()->is_thrusting = false;
                }
            }

            get_player()->input_axis = v2_normalize(get_player()->input_axis);
            // float angle = get_entity_angle(get_player());
            float thrust_mult = (get_player()->is_thrusting && get_player()->energy.current > 0) ? 10 : 1;
            get_player()->move_vec = v2_mulf(get_player()->input_axis, thrust_mult * get_player()->move_speed);
            get_player()->energy.rate = -1 * thrust_mult * v2_length(get_player()->input_axis);
        }

        //: entity loop
        {
            for (int i = 0; i < MAX_ENTITY_COUNT; i++) {
                Entity *en = &world->entities[i];
                if (en->is_valid) {
                    switch (en->arch) {
                    case ARCH_player:
                        set_world_space();
                        push_z_layer(layer_entity);
                        //: physics
                        {
                            en->last_momentum = en->momentum;

                            push_z_layer(layer_en_debug);
                            draw_line(get_entity_midpoint(en),
                                      v2_add(get_entity_midpoint(en), v2_mulf(en->move_vec, 0.05)), 1, COLOR_YELLOW);
                            pop_z_layer();
                            en->momentum = v2_add(en->momentum, v2_mulf(en->move_vec, delta_t));
                            en->velocity = v2_divf(en->momentum, en->mass);

                            for (int j = 0; j < MAX_ENTITY_COUNT; j++) {
                                if (i != j) {
                                    Entity *other_en = &world->entities[j];
                                    solid_entity_collision(en, other_en);
                                }
                            }

                            float torque = 0;
                            en->angular_velocity += delta_t * torque / en->mass;
                            en->orientation += en->angular_velocity * delta_t;
                            // en->orientation += delta_t;
                            en->pos = v2_add(en->pos, v2_mulf(en->velocity, delta_t));
                            en->energy.current += en->energy.rate * delta_t;
                        }
                        render_sprite_entity(en);
                        break;
                    case ARCH_planet:
                        set_world_space();
                        push_z_layer(layer_entity);
                        render_sprite_entity(en);
                        break;
                    default:
                        set_world_space();
                        push_z_layer(layer_entity);
                        render_sprite_entity(en);
                        break;
                    }
                    pop_z_layer();
                }
            }
        }

        particle_update();
        particle_render();

        // :tile rendering
        {
            set_world_space();
            push_z_layer(layer_stage_fg);
            int player_tile_x = world_pos_to_tile_pos(get_player()->pos.x);
            int player_tile_y = world_pos_to_tile_pos(get_player()->pos.y);
            int tile_radius_x = 40;
            int tile_radius_y = 30;
            for (int x = player_tile_x - tile_radius_x; x < player_tile_x + tile_radius_x; x++) {
                for (int y = player_tile_y - tile_radius_y; y < player_tile_y + tile_radius_y; y++) {
                    if ((x + (y % 2 == 0)) % 2 == 0) {
                        Vector4 col = v4(0.1, 0.1, 0.1, 0.1);
                        float x_pos = x * tile_width;
                        float y_pos = y * tile_width;
                        draw_rect(v2(x_pos + tile_width * -0.5, y_pos + tile_width * -0.5), v2(tile_width, tile_width),
                                  col);
                    }
                }
            }

            pop_z_layer();
        }
        set_world_space();
        push_z_layer(layer_stage_fg);
        Matrix4 xform = m4_scalar(1.0);
        xform = m4_translate(xform, v3(-1000, -1000, 0));
        draw_circle_xform(xform, v2(2000, 2000), v4(0.1, 0.1, 0.1, 0.999999999));
        pop_z_layer();

        if (v2_length(get_player()->pos) > 1000) {
            world->ux_state = UX_win;
        }
        if (get_player()->energy.current < 0.0) {
            world->ux_state = UX_lose;
        }
        //: ui
        if (world->ux_state == UX_win) {
            string text = STR("You Win!");
            set_screen_space();
            push_z_layer(layer_text);
            Matrix4 xform = m4_scalar(1.0);
            xform = m4_translate(xform, v3(screen_width / 4.0, screen_height / 2.0, 0));
            draw_text_xform(font, text, font_height, xform, v2(0.5, 0.5), COLOR_YELLOW);
        } else if (world->ux_state == UX_lose) {
            string text = STR("You Lose!");
            set_screen_space();
            push_z_layer(layer_text);
            Matrix4 xform = m4_scalar(1.0);
            xform = m4_translate(xform, v3(screen_width / 4.0, screen_height / 2.0, 0));
            draw_text_xform(font, text, font_height, xform, v2(0.5, 0.5), COLOR_RED);
        }

        //: bar rendering
        {
            set_screen_space();
            push_z_layer(layer_ui_fg);
            Matrix4 xform = m4_scalar(1.0);
            xform = m4_translate(xform, v3(0, screen_height - 10, 0));
            draw_rect_xform(xform, v2(25, 0.5), COLOR_GREY);
            draw_rect_xform(xform, v2((get_player()->energy.current / get_player()->energy.max) * 25.0f, 0.5),
                            COLOR_GREEN);
            pop_z_layer();
        }

        //: timer
        if (debug_render) {
            {
                seconds_counter += delta_t;
                if (world->ux_state != UX_win && world->ux_state != UX_lose) {
                    world->time_elapsed += delta_t;
                }
                frame_count += 1;
                if (seconds_counter > 1.0) {
                    last_fps = frame_count;
                    frame_count = 0;
                    seconds_counter = 0.0;
                }
                string text = STR("fps: %i time: %.2f speed: %.2f");
                text = sprint(temp_allocator, text, last_fps, world->time_elapsed, v2_length(get_player()->velocity));
                set_screen_space();
                push_z_layer(layer_text);
                Matrix4 xform = m4_scalar(1.0);
                xform = m4_translate(xform, v3(0, screen_height - (font_height * 0.1), 0));
                draw_text_xform(font, text, font_height, xform, v2(0.1, 0.1), COLOR_RED);
            }
        }

        particle_update();
        particle_render();
        os_update();
        gfx_update();

        // load/save commands
        // these are at the bottom, because we'll want to have a clean spot to
        // do this to avoid any mid-way operation bugs.
#if CONFIGURATION == DEBUG
        {
            if (is_key_just_pressed('F')) {
                world_save_to_disk();
                log("saved");
            }
            if (is_key_just_pressed('L')) {
                world_attempt_load_from_disk();
                log("loaded ");
            }
            if (is_key_just_pressed('K') && is_key_down(KEY_SHIFT)) {
                memset(world, 0, sizeof(World));
                setup_world();
                log("reset");
            }
        }
#endif
    }

    // world_save_to_disk();

    return 0;
}
