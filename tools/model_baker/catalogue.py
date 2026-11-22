"""catalogue.py - Master registry and ordered collection of all 3D engine models."""

from model import Model


def build_catalogue() -> list[Model]:
    """Collect all registered models in the locked catalogue order:

    infantry, vehicle, air, naval, building, prop, resource, projectile.
    """
    all_models: list[Model] = []

    # Ground: Infantry + Vehicle
    try:
        import tables_ground
        all_models.extend(tables_ground.MODELS)
    except ImportError:
        pass

    # Air + Naval
    try:
        import tables_air_naval
        all_models.extend(tables_air_naval.MODELS)
    except ImportError:
        pass

    # Building + Prop + Resource + Projectile
    try:
        import tables_static
        all_models.extend(tables_static.MODELS)
    except ImportError:
        pass

    return all_models
