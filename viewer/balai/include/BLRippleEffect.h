#ifndef BL_RIPPLE_EFFECT_H
#define BL_RIPPLE_EFFECT_H

namespace mlabs { namespace balai { namespace graphics {

class ShaderEffect;
class RenderSurface;
class ITexture;

namespace shader {
	struct Constant;
	struct Sampler;
}

class RippleEffect {
	enum { MAX_DROPLETS = 64 };
	struct {
		float x, y;
		float time;
		float height;
	} droplets_[MAX_DROPLETS];
	int num_droplets_{0};

	ShaderEffect* shader_{nullptr};
	shader::Constant const* envCoeff_{nullptr};
	shader::Constant const* dropletCoeff_{nullptr};
	shader::Sampler const* sourceMap_{nullptr};

public:
	RippleEffect(RippleEffect const&) = delete;
	RippleEffect& operator=(RippleEffect const&) = delete;
	RippleEffect() = default;
	~RippleEffect() { Clear(); }

	void AddDroplet(float x, float y, float impact=1.0f);

	bool Initialize();
	bool Simulate(float, ITexture* source, RenderSurface* dest);
	void Clear();
};

}}} // namespace mlabs::balai::graphics

#endif
