#include "NoiseFilter.hpp"

ossia::value NoiseFilter::filter(const ossia::value& val)
{
    struct vis
    {
            ossia::value operator()() const { return {}; }
            ossia::value operator()(const ossia::impulse&) const { return {}; }

            ossia::value operator()(int i) const
            {
                return i;
            }
            ossia::value operator()(float f) const
            {
                return f;
            }
            ossia::value operator()(bool b) const
            {
                return b;
            }

            ossia::value operator()(const std::string& s) const
            {
                return s;
            }

            ossia::value operator()(const ossia::vec2f& t) const
            {
                return one_euro_v2(t, 1);
            }

            ossia::value operator()(const ossia::vec3f& t) const
            {
                return t;
            }

            ossia::value operator()(const ossia::vec4f& t) const
            {
                return t;
            }

            ossia::value operator()(const std::vector<ossia::value>& t) const
            {
                std::vector<ossia::value> res;

                for (auto& v : t)
                {
                    res.push_back(ossia::apply(*this, v.v));
                }

                return res;
            }
    };

    try {
        return ossia::apply(vis{}, val.v);
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
    } catch(...) {
        std::cerr << "error" << std::endl;
    }
    return val;
}
