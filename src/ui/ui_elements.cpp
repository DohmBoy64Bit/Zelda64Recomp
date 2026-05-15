#include "ui_elements.h"

struct RecompCustomElement {
    Rml::String tag;
    std::unique_ptr<Rml::ElementInstancer> instancer;
};

#define CUSTOM_ELEMENT(s, e) { s, std::make_unique< Rml::ElementInstancerGeneric< e > >() }

// Lazy-init: Rml types must not run during static init (before Rml::Initialise in ui_state.cpp).
static RecompCustomElement* custom_elements() {
    static RecompCustomElement elements[] = {
        CUSTOM_ELEMENT("recomp-mod-menu", recompui::ElementModMenu),
        CUSTOM_ELEMENT("recomp-config-sub-menu", recompui::ElementConfigSubMenu),
    };
    return elements;
}

static constexpr size_t custom_elements_count() {
    return 2;
}

void recompui::register_custom_elements() {
    for (size_t i = 0; i < custom_elements_count(); ++i) {
        auto& element_config = custom_elements()[i];
        Rml::Factory::RegisterElementInstancer(element_config.tag, element_config.instancer.get());
    }
}

Rml::ElementInstancer* recompui::get_custom_element_instancer(std::string tag) {
    for (size_t i = 0; i < custom_elements_count(); ++i) {
        auto& element_config = custom_elements()[i];
        if (tag == element_config.tag) {
            return element_config.instancer.get();
        }
    }
    return nullptr;
}

Rml::ElementPtr recompui::create_custom_element(Rml::Element* parent, std::string tag) {
    auto instancer = recompui::get_custom_element_instancer(tag);
    const Rml::XMLAttributes attributes = {};
    if (Rml::ElementPtr element = instancer->InstanceElement(parent, tag, attributes))
    {
        element->SetInstancer(instancer);
        element->SetAttributes(attributes);

        return element;
    }

    return nullptr;
}
