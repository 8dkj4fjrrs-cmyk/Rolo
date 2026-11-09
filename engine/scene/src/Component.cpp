#include "rolo/scene/Component.hpp"
#include "rolo/core/Names.hpp"
#include "rolo/scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace rolo {

namespace {

float clampFloat(float v, const PropertyOptions& o)
{
    if (!std::isfinite(v)) {
        v = 0.0f;
    }
    if (o.min) {
        v = std::max(v, *o.min);
    }
    if (o.max) {
        v = std::min(v, *o.max);
    }
    return v;
}

std::optional<float> asNumber(const PropertyValue& value)
{
    if (const float* f = std::get_if<float>(&value)) {
        return *f;
    }
    if (const int* i = std::get_if<int>(&value)) {
        return static_cast<float>(*i);
    }
    return std::nullopt;
}

} // namespace

std::optional<PropertyValue> PropertyInfo::coerce(const PropertyValue& value) const
{
    switch (kind) {
    case PropertyKind::Bool:
        if (const bool* b = std::get_if<bool>(&value)) {
            return *b;
        }
        return std::nullopt;
    case PropertyKind::Float:
        if (const auto n = asNumber(value)) {
            return clampFloat(*n, options);
        }
        return std::nullopt;
    case PropertyKind::Int:
        if (const auto n = asNumber(value)) {
            return static_cast<int>(std::lround(clampFloat(*n, options)));
        }
        return std::nullopt;
    case PropertyKind::Vec2:
        if (const Vec2* v = std::get_if<Vec2>(&value)) {
            return Vec2{clampFloat(v->x, options), clampFloat(v->y, options)};
        }
        return std::nullopt;
    case PropertyKind::Color:
        if (const Color* c = std::get_if<Color>(&value)) {
            return *c;
        }
        return std::nullopt;
    case PropertyKind::String:
        if (const std::string* s = std::get_if<std::string>(&value)) {
            return *s;
        }
        return std::nullopt;
    case PropertyKind::Asset:
        if (const AssetId* id = std::get_if<AssetId>(&value)) {
            return *id; // whether the asset exists is checked by the asset database
        }
        return std::nullopt;
    case PropertyKind::Choice:
        if (const int* i = std::get_if<int>(&value)) {
            if (*i >= 0 && static_cast<std::size_t>(*i) < options.choices.size()) {
                return *i;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

const PropertyInfo* ComponentType::findProperty(std::string_view key) const
{
    for (const PropertyInfo& p : properties) {
        if (p.key == key) {
            return &p;
        }
    }
    return nullptr;
}

std::optional<PropertyValue> Component::get(std::string_view key) const
{
    const PropertyInfo* p = type().findProperty(key);
    if (p == nullptr || !p->get) {
        return std::nullopt;
    }
    return p->get(*this);
}

bool Component::set(std::string_view key, const PropertyValue& value)
{
    const PropertyInfo* p = type().findProperty(key);
    if (p == nullptr || !p->set) {
        return false;
    }
    const auto coerced = p->coerce(value);
    if (!coerced) {
        return false;
    }
    p->set(*this, *coerced);
    return true;
}

const ComponentType& UnknownComponent::type() const
{
    static const ComponentType unknown = [] {
        ComponentType t;
        t.id = std::string(kTypeId);
        t.displayName = "Unknown Component";
        t.description = "A component this version of Rolo does not recognise. Its data is kept "
                        "unchanged and saved again with the scene.";
        return t;
    }();
    return unknown;
}

// --- Registry ------------------------------------------------------------------

ComponentRegistry ComponentRegistry::withBuiltins()
{
    ComponentRegistry registry;
    static_cast<void>(registry.add(Transform::componentType()));
    static_cast<void>(registry.add(Shape::componentType()));
    static_cast<void>(registry.add(Sprite::componentType()));
    static_cast<void>(registry.add(Camera::componentType()));
    static_cast<void>(registry.add(Tilemap::componentType()));
    static_cast<void>(registry.add(Collider::componentType()));
    static_cast<void>(registry.add(RigidBody::componentType()));
    static_cast<void>(registry.add(Text::componentType()));
    static_cast<void>(registry.add(Animator::componentType()));
    return registry;
}

Status ComponentRegistry::add(const ComponentType& type)
{
    if (type.id.empty()) {
        return Status::failure("A component type needs an id.");
    }
    if (find(type.id) != nullptr) {
        return Status::failure("The component type '" + type.id + "' is already registered.");
    }
    m_types.push_back(&type);
    return Status::success();
}

const ComponentType* ComponentRegistry::find(std::string_view id) const
{
    for (const ComponentType* t : m_types) {
        if (t->id == id) {
            return t;
        }
    }
    // Files saved by Lantern (Rolo's old name) say "lantern.Transform" where
    // Rolo says "rolo.Transform" (rolo/core/Names.hpp).
    if (const auto upgraded = upgradeLegacyName(id)) {
        return find(*upgraded);
    }
    return nullptr;
}

// --- Built-in components ------------------------------------------------------------

const ComponentType& Transform::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Transform>("rolo.Transform", "Transform")
            .description("Where the object is, relative to its parent. Position is in points from the "
                         "parent's origin (y points up), rotation in degrees clockwise, and scale "
                         "multiplies the size (1 is normal size).")
            .required()
            .property("position", "Position", &Transform::position, {.step = 1.0f})
            .property("rotation", "Rotation", &Transform::rotation,
                      {.step = 1.0f, .help = "Degrees, clockwise.", .unit = "\u00B0"})
            .property("scale", "Scale", &Transform::scale, {.step = 0.1f})
            .build();
    return type;
}

const ComponentType& Shape::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Shape>("rolo.Shape", "Shape")
            .description("Draws a rectangle or an ellipse centred on the object, filled or as an outline.")
            .property("kind", "Kind", &Shape::kind, {.choices = {"rectangle", "ellipse"}})
            .property("size", "Size", &Shape::size, {.min = 0.0f, .step = 1.0f})
            .property("color", "Color", &Shape::color)
            .property("filled", "Filled", &Shape::filled)
            .property("lineWidth", "Line Width", &Shape::lineWidth,
                      {.min = 0.0f, .max = 100.0f, .step = 0.5f, .help = "Outline thickness, when not filled.", .unit = "pt"})
            .build();
    return type;
}

const ComponentType& Sprite::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Sprite>("rolo.Sprite", "Sprite")
            .description("Draws an image centred on the object, stretched to Size. Color tints the image "
                         "(white keeps its own colours); its alpha makes it see-through.")
            .property("image", "Image", &Sprite::image,
                      {.help = "An image asset from the project.", .assetType = "image"})
            .property("size", "Size", &Sprite::size, {.min = 0.0f, .step = 1.0f})
            .property("color", "Color", &Sprite::color)
            .property("flipX", "Flip X", &Sprite::flipX, {.help = "Mirror the image left to right."})
            .property("flipY", "Flip Y", &Sprite::flipY, {.help = "Mirror the image top to bottom."})
            .build();
    return type;
}

const ComponentType& Camera::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Camera>("rolo.Camera", "Camera")
            .description("Decides what the player sees: the game view is centred on this object. The first "
                         "active Camera in the scene is used. Rotation and scale of the object are ignored.")
            .property("zoom", "Zoom", &Camera::zoom,
                      {.min = 0.05f, .max = 20.0f, .step = 0.1f, .help = "2 shows everything twice as large."})
            .build();
    return type;
}

const ComponentType& Collider::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Collider>("rolo.Collider", "Collider")
            .description("A box for collision. Solid colliders block movement; any collider can be found by "
                         "gameplay code, e.g. to notice the player walking through a door.")
            .property("size", "Size", &Collider::size, {.min = 0.0f, .help = "The box's width and height, in points."})
            .property("offset", "Offset", &Collider::offset,
                      {.help = "Moves the box away from the object's centre, e.g. to cover only a character's feet."})
            .property("solid", "Solid", &Collider::solid,
                      {.help = "Solid colliders block movement. Turn off for areas such as doors and triggers."})
            .property("shape", "Shape", &Collider::shape,
                      {.choices = {"box", "circle", "capsule"},
                       .help = "Box, the largest circle inside the box, or a capsule (the box with round ends: good "
                               "for characters). The shape matters to physics; other collision uses the box."})
            .build();
    return type;
}

const ComponentType& RigidBody::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<RigidBody>("rolo.RigidBody", "Rigid Body")
            .category("Physics")
            .description("Physics moves this object: it falls with the scene's gravity, collides, is pushed and "
                         "bounces. Needs a Collider (its shape). Set the gravity on the scene (select the scene "
                         "in the Hierarchy).")
            .property("bodyType", "Type", &RigidBody::type,
                      {.choices = {"dynamic", "kinematic", "static"},
                       .help = "Dynamic: physics moves it. Kinematic: only code moves it (setVelocity), and it "
                               "pushes dynamic bodies, like a moving platform. Static: never moves."})
            .property("mass", "Mass", &RigidBody::mass,
                      {.min = 0.001f, .step = 0.1f, .help = "How heavy it is: heavy things push light things.", .unit = "kg"})
            .property("gravityScale", "Gravity Scale", &RigidBody::gravityScale,
                      {.step = 0.1f, .help = "1: the scene's gravity. 0: floats. Negative: falls upwards."})
            .property("friction", "Friction", &RigidBody::friction,
                      {.min = 0.0f, .max = 2.0f, .step = 0.05f, .help = "0: slides like ice. 1: grips like rubber."})
            .property("bounciness", "Bounciness", &RigidBody::bounciness,
                      {.min = 0.0f, .max = 1.0f, .step = 0.05f, .help = "0: lands with a thud. 1: bounces back as high."})
            .property("drag", "Drag", &RigidBody::drag,
                      {.min = 0.0f, .step = 0.1f, .help = "Slows it down as it moves, like air or water."})
            .property("lockRotation", "Lock Rotation", &RigidBody::lockRotation,
                      {.help = "Never turns over: for characters."})
            .property("fast", "Fast Moving", &RigidBody::fast,
                      {.help = "For small, fast things (bullets): checked continuously so they never pass "
                               "through walls. Costs a little more."})
            .build();
    return type;
}

const ComponentType& Text::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Text>("rolo.Text", "Text")
            .description("Text in the world, or on screen as a child of the Camera. {variable} shows a game "
                         "variable's value while playing. Source fills it from the game instead: the dialogue, "
                         "the interaction prompt or the quest log.")
            .property("text", "Text", &Text::text, {.help = "What it says. {gems} shows the variable gems."})
            .property("source", "Source", &Text::source,
                      {.choices = {"text", "dialogueSpeaker", "dialogueLine", "dialogueChoices", "interactionPrompt",
                                   "questLog"},
                       .help = "Where the words come from while the game runs."})
            .property("font", "Font", &Text::font, {.help = "A font file (.ttf). None: Rolo's own font.", .assetType = "font"})
            .property("size", "Size", &Text::size, {.min = 4.0f, .max = 512.0f, .unit = "pt"})
            .property("color", "Color", &Text::color)
            .property("align", "Align", &Text::align, {.choices = {"left", "center", "right"}})
            .property("width", "Wrap Width", &Text::width,
                      {.min = 0.0f, .help = "Long text wraps onto new lines at this width. 0: no wrapping.", .unit = "pt"})
            .build();
    return type;
}

const ComponentType& Animator::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Animator>("rolo.Animator", "Animator")
            .description("Plays a flipbook clip on the object's Sprite (the Sprite keeps its size, colour and "
                         "flipping). Clips come from an animation asset (.lanim); gameplay code switches clips.")
            .property("animations", "Animations", &Animator::animations,
                      {.help = "The animation set: a sprite sheet and its clips.", .assetType = "animation"})
            .property("clip", "Clip", &Animator::clip, {.help = "The clip it plays, e.g. walk_down."})
            .property("speed", "Speed", &Animator::speed,
                      {.min = 0.0f, .max = 20.0f, .step = 0.1f, .help = "2 plays twice as fast."})
            .property("playing", "Playing", &Animator::playing, {.help = "Off: shows the clip's first frame."})
            .build();
    return type;
}

} // namespace rolo
