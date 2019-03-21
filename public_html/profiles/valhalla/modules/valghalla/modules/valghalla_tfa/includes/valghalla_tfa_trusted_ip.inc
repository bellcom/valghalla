<?php

/**
 * @file
 * Classes for Valghalla TFA.
 */

/**
 * Class TfaTrustedBrowser.
 */
class TfaTrustedIp extends TfaBasePlugin implements TfaLoginPluginInterface {

  /**
   * Check login allowance function by IP.
   *
   * @return bool
   *   TRUE if IP is in range, FALSE otherwise.
   */
  public function loginAllowed() {
    $trusted_ips = explode(',', variable_get('valghalla_tfa_trusted_ip'));
    $remote_ip = ip_address();

    // Is trusted IP?
    if (in_array($remote_ip, $trusted_ips)) {
      return TRUE;
    }
    return FALSE;
  }

}
