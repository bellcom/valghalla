<?php

/**
 * Class KkLogin.
 */
class KkLogin {
  private $nemloginUrl;
  private $logoutRedirectUrl;

  /**
   * KkLogin constructor.
   *
   * @param string $nemloginUrl
   *   Login URL.
   * @param string $logoutRedirectUrl
   *   Logout URL.
   */
  public function __construct($nemloginUrl, $logoutRedirectUrl) {
    $this->nemloginUrl = $nemloginUrl;
    $this->logoutRedirectUrl = $logoutRedirectUrl;
  }

  /**
   * Get subdomain function.
   */
  public function getSubDomain() {
    return $_SERVER['SERVER_NAME'];
  }

  /**
   * Get cookie domain function.
   */
  public function getCookieDomain() {
    return "." . $this->getSubDomain();
  }

  /**
   * Cookie check function.
   */
  public function haveCookie() {
    return $this->getCookie() != NULL;
  }

  /**
   * Get cookie function.
   */
  public function getCookie() {
    return $_COOKIE["nemloginkk"];
  }

  /**
   * Update cookie function.
   */
  public function updateCookie($samlid) {
    setcookie('nemloginkk', $samlid, time() + 3600 * 24, '/', $this->getCookieDomain(), TRUE);
  }

  /**
   * Check logged in function.
   */
  public function isLoggedIn() {
    return file_get_contents($this->nemloginUrl . '/nemlogin/loggedInService/' . $this->getCookie()) == "true";
  }

  /**
   * Get login url function.
   */
  public function getLoginUrl() {
    global $base_url;
    $b64Url = base64_encode($base_url . "/nemlogin/handlepost");
    return $this->nemloginUrl . '/nemlogin/login?callbackurl=' . $b64Url;
  }

  /**
   * Logout function.
   */
  public function logout() {
    setcookie("nemloginkk", "", time() - 3600, "/", "." . $this->getSubDomain());
    header('Location: ' . $this->getLogoutUrl());
  }

  /**
   * Get logout url function.
   */
  public function getLogoutUrl() {
    $b64Url = base64_encode($this->logoutRedirectUrl);
    return $this->nemloginUrl . '/nemlogin/logout?callbacklogout=' . $b64Url;
  }

  /**
   * Get logged in status function.
   */
  public function getLoggedInStatus() {
    $response = file_get_contents($this->nemloginUrl . '/nemlogin/loggedInUserService/' . $this->getCookie());
    $response = json_decode($response);

    return $response;
  }

}
