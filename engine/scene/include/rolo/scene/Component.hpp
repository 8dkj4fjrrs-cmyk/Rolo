#pragma once

// Components: the pieces of data and behaviour attached to an entity.
//
// Rolo keeps the component model simple (no ECS framework): an entity owns
// a short list of component objects, at most one of each type. Every entity
// has a Transform; other components are optional.
//
// Each component type *describes its properties* once (name, label, kind,
// limits). That description is what lets the editor show any component in the
// Inspector, save and load it, and undo changes to it, without code written
// specially for each type:
//
//     struct Shape : ComponentBase<Shape> {
//         Vec2 size{100, 100};
//         static const ComponentType& componentType();
//     };
//     const ComponentType& Shape::componentType() {
//         static const ComponentType type = ComponentTypeBuilder<Shape>("rolo.Shape", "Shape")
//             .property("size", "Size", &Shape::size, {.min = 0.0f})
//             .build();
//         return type;
//     }

#include "rolo/scene/EntityId.hpp"
#include "rolo/scene/Types.hpp"

#include "rolo/core/Engine.hpp" // Status

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace rolo {

class Component;

enum class PropertyKind { Bool, Int, Float, Vec2, Color, String, Choice, Asset };

/// A property value of any supported kind. Choice properties hold the index
/// of the chosen option (saved in files by name, so options can be reordered).
using PropertyValue = std::variant<bool, int, float, Vec2, Color, std::string, AssetId>;

/// Optional settings when describing a property.
struct PropertyOptions {
    std::optional<float> min;           // Int, Float, Vec2 (each axis): smallest allowed
    std::optional<float> max;           // ... largest allowed
    float step = 1.0f;                  // editor nudge with the arrow keys
    std::vector<std::string> choices;   // Choice: the option names saved in files
    std::string help;                   // one sentence for tooltips
    std::string unit;                   // shown after the value, e.g. "°"
    std::string assetType;              // Asset: the kind of asset accepted, e.g. "image"
};

struct PropertyInfo {
    std::string key;     // saved in files; never change it once released
    std::string label;   // shown in the editor
    PropertyKind kind = PropertyKind::Float;
    PropertyOptions options;
    std::function<PropertyValue(const Component&)> get;
    std::function<void(Component&, const PropertyValue&)> set;   // receives a coerced value

    /// The value made valid for this property: right kind (an int is accepted
    /// for a float and vice versa), clamped to min/max, a choice in range.
    /// Nothing if the value cannot be used at all (e.g. text for a number).
    std::optional<PropertyValue> coerce(const PropertyValue& value) const;
};

class ComponentType {
public:
    std::string id;            // e.g. "rolo.Shape"; saved in files
    std::string displayName;   // e.g. "Shape"
    std::string description;   // one or two sentences for the editor
    std::string category;      // groups the editor's Add Component menu, e.g. "Gameplay"; empty = Rolo's own
    bool required = false;     // every entity has one; it cannot be removed
    std::vector<PropertyInfo> properties;
    std::function<std::unique_ptr<Component>()> create;

    const PropertyInfo* findProperty(std::string_view key) const;
};

class Component {
public:
    virtual ~Component() = default;

    virtual const ComponentType& type() const = 0;
    virtual std::unique_ptr<Component> clone() const = 0;

    std::optional<PropertyValue> get(std::string_view key) const;
    /// Sets a property (coerced, see PropertyInfo::coerce). False if there is
    /// no such property or the value cannot be used.
    bool set(std::string_view key, const PropertyValue& value);

    /// Data a component keeps besides its described properties (e.g. a
    /// tilemap's tiles), as JSON text; empty if there is none. Scene files
    /// store it under the component's "data" key.
    virtual std::string saveData() const { return {}; }
    /// Reads what saveData() wrote. Returns a description of anything that
    /// could not be read (empty = all fine); readable parts are kept.
    virtual std::string loadData(std::string_view /*json*/) { return {}; }

protected:
    Component() = default;
    Component(const Component&) = default;
    Component& operator=(const Component&) = default;
};

/// Base for concrete components: provides type() and clone().
template <class Derived>
class ComponentBase : public Component {
public:
    const ComponentType& type() const override { return Derived::componentType(); }
    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

/// A component whose type this program does not know (e.g. saved by a newer
/// editor or a game module that is not loaded). Its data is kept unchanged and
/// written back when the scene is saved, so nothing is lost.
class UnknownComponent final : public Component {
public:
    UnknownComponent(std::string typeId, std::string json)
        : m_typeId(std::move(typeId))
        , m_json(std::move(json))
    {
    }
    const ComponentType& type() const override;
    std::unique_ptr<Component> clone() const override { return std::make_unique<UnknownComponent>(*this); }

    const std::string& originalTypeId() const noexcept { return m_typeId; }
    /// The one ComponentType every unknown component has (what type() returns).
    static const ComponentType& unknownType();
    /// The component exactly as it was in the file (a JSON object).
    const std::string& json() const noexcept { return m_json; }

    static constexpr std::string_view kTypeId = "rolo.Unknown";

private:
    std::string m_typeId;
    std::string m_json;
};

// --- Describing component types ----------------------------------------------------------

namespace detail {
template <class M>
constexpr PropertyKind propertyKindOf()
{
    if constexpr (std::is_same_v<M, bool>) {
        return PropertyKind::Bool;
    } else if constexpr (std::is_same_v<M, int>) {
        return PropertyKind::Int;
    } else if constexpr (std::is_same_v<M, float>) {
        return PropertyKind::Float;
    } else if constexpr (std::is_same_v<M, Vec2>) {
        return PropertyKind::Vec2;
    } else if constexpr (std::is_same_v<M, Color>) {
        return PropertyKind::Color;
    } else if constexpr (std::is_same_v<M, std::string>) {
        return PropertyKind::String;
    } else if constexpr (std::is_same_v<M, AssetId>) {
        return PropertyKind::Asset;
    } else {
        static_assert(std::is_enum_v<M>, "unsupported property type");
        return PropertyKind::Choice;
    }
}
} // namespace detail

template <class T>
class ComponentTypeBuilder {
public:
    ComponentTypeBuilder(std::string id, std::string displayName)
    {
        m_type.id = std::move(id);
        m_type.displayName = std::move(displayName);
        m_type.create = [] { return std::make_unique<T>(); };
    }

    ComponentTypeBuilder& description(std::string text)
    {
        m_type.description = std::move(text);
        return *this;
    }

    /// Groups the type in the editor's Add Component menu (e.g. "Gameplay").
    ComponentTypeBuilder& category(std::string name)
    {
        m_type.category = std::move(name);
        return *this;
    }

    ComponentTypeBuilder& required()
    {
        m_type.required = true;
        return *this;
    }

    /// Describes the member `member` as a property.
    template <class M>
    ComponentTypeBuilder& property(std::string key, std::string label, M T::*member, PropertyOptions options = {})
    {
        // Scene files write a component as {"type": ..., "data": ..., <properties>}:
        // a property with either name would be mistaken for them. A mistake in
        // Rolo's (or a game module's) code, caught by any test using the component.
        if (key == "type" || key == "data") {
            throw std::logic_error("Component " + m_type.id + ": a property cannot be called \"" + key
                                   + "\" (scene files use that name).");
        }
        PropertyInfo info;
        info.key = std::move(key);
        info.label = std::move(label);
        info.kind = detail::propertyKindOf<M>();
        info.options = std::move(options);
        info.get = [member](const Component& c) -> PropertyValue {
            const M& value = static_cast<const T&>(c).*member;
            if constexpr (std::is_enum_v<M>) {
                return static_cast<int>(value);
            } else {
                return value;
            }
        };
        info.set = [member](Component& c, const PropertyValue& v) {
            M& target = static_cast<T&>(c).*member;
            if constexpr (std::is_enum_v<M>) {
                target = static_cast<M>(std::get<int>(v));
            } else {
                target = std::get<M>(v);
            }
        };
        m_type.properties.push_back(std::move(info));
        return *this;
    }

    ComponentType build() { return std::move(m_type); }

private:
    ComponentType m_type;
};

/// The component types a program knows about. Built-in types are always
/// there; game modules add their own behaviours (see rolo/gameplay).
class ComponentRegistry {
public:
    /// A registry with Rolo's built-in components (Transform, Shape).
    static ComponentRegistry withBuiltins();

    /// Adds a type. Fails if its id is empty or already registered. The type
    /// must outlive the registry (types are usually function-local statics).
    Status add(const ComponentType& type);
    const ComponentType* find(std::string_view id) const;
    /// In the order they were added.
    const std::vector<const ComponentType*>& types() const noexcept { return m_types; }

private:
    std::vector<const ComponentType*> m_types;
};

} // namespace rolo
