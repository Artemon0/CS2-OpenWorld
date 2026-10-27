# Workshop resources

Publish maps and shared gameplay/UI assets as separate Workshop packages. Keep map-specific resources with the corresponding map package and reusable particle, sound, and material resources in the shared package. The server plugin must not assume that publishing one package implicitly installs the other.

The particle menu currently has no bundled VPCF/VTEX resources. Its runtime adapter must be validated against a Workshop-delivered prototype before production resources are added.
