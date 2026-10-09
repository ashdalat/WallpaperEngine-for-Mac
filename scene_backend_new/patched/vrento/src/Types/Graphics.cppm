export module vrento.graphics_types;

export namespace vrento
{

enum class BlendMode
{
    Disable,
    Translucent,
    Additive,
    AlphaToCoverage,
    Normal
};
enum class CullMode
{
    None,
    Front,
    Back
};
enum class MeshPrimitive
{
    POINT,
    TRIANGLE
};
enum class FillMode
{
    STRETCH,
    ASPECTFIT,
    ASPECTCROP
};

} // namespace vrento
