<?php

define('NEMLOGIN_IDP_LOGINSERVICE_PATH', '/service/loginservice.wsdl');
define('NEMLOGIN_IDP_LOGIN_PATH', '/nemlogin.php');
define('NEMLOGIN_IDP_LOGOUT_PATH', '/nemlogout.php');
define('NEMLOGIN_IDP_FETCH_ONLY', TRUE);

/**
 * Ctools plugin class NemloginAuthIDP.
 */
class NemloginAuthIDP extends NemloginAuth {

  /**
   * Identity provider URL.
   *
   * @var string
   */
  private $idpUrl;

  /**
   * Fetch only mode flag.
   *
   * @var bool
   */
  private $fetchOnly;

  /**
   * Authorization object.
   *
   * @var SoapClient
   */
  private $soapClient;

  /**
   * NemloginAuthSimpleSaml constructor.
   *
   * @throws Exception
   */
  public function __construct() {
    $this->idpUrl = variable_get('nemlogin_idp_url');
    $this->fetchOnly = variable_get('nemlogin_idp_fetch_only', NEMLOGIN_IDP_FETCH_ONLY);

    // Authentification values stored in session.
    if (!isset($_SESSION['nemlogin_idp'])) {
      $_SESSION['nemlogin_idp'] = array();
    }
    $this->values = &$_SESSION['nemlogin_idp'];

    // Init authentication object.
    try {
      $this->soapClient = new SoapClient($this->idpUrl . NEMLOGIN_IDP_LOGINSERVICE_PATH);
    }
    catch (Exception $e) {
      watchdog('Valghalla nemlogin', 'Cannot initialize auth SOAP object: @message', array('@message' => $e->getMessage()), WATCHDOG_ERROR);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function isOk() {
    return $this->soapClient instanceof SoapClient;
  }

  /**
   * {@inheritdoc}
   */
  public function login() {
    if (empty($_REQUEST['token'])) {
      global $base_url;
      $forward_url = $base_url . request_uri();
      $url = url($this->idpUrl . NEMLOGIN_IDP_LOGIN_PATH, array(
        'query' => array(
          'mnemo' => 'bellcom.dk',
          'forward' => $forward_url,
        ),
      ));
      header('Location: ' . $url, TRUE, 302);
      drupal_exit($url);
    }

    $token = $_REQUEST['token'];
    $mnemo = $_REQUEST['mnemo'];

    $response = NULL;
    $cpr = NULL;

    try {
      $response = $this->soapClient->LogIn(array('token' => $token, 'mnemo' => $mnemo));
    }
    catch (Exception $e) {
      watchdog('Valghalla nemlogin', 'Cannot initialize request: @message', array('@message' => $e->getMessage()), WATCHDOG_ERROR);
    }

    if ($response && isset($response->LogInResult)) {
      $cprraw = $response->LogInResult->cpr;
      if ($cprraw) {
        $cpr = utf8_decode($cprraw);
        // Adding dash, if missing. E.g. 123456-7890.
        if (preg_match('/[0-9]{10}/', $cpr)) {
          $cpr = substr($cpr, 0, 6) . '-' . substr($cpr, 6, 4);
        }

        // We need to store CPR value between redirects
        // to be able use it in a a signup form.
        // This value will be deleted after first usage.
        // @see $this->fetchValue() method.
        $this->values['cpr'] = $cpr;
      }
    }

    if (!$cpr) {
      watchdog('nemlogin Idp', 'Could not fetch CPR. Response is empty', array(), WATCHDOG_WARNING);
    }

    if ($this->fetchOnly) {
      $return_to_url = $this->getReturnUrl();
      $this->destroySession($return_to_url);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function logout() {
    // Reset all values.
    $this->values = NULL;

    $this->destroySession($this->getReturnUrl());
  }

  /**
   * Destroys identity provider session.
   *
   * @param string $callback
   *   Callback URL.
   */
  private function destroySession($callback) {
    $getParams = http_build_query(
      array(
        'RelayState' => $callback,
      )
    );
    $idp = variable_get('nemlogin_idp_url', 'https://nemlogin.bellcom.dk/simplesaml');
    $url = $idp . NEMLOGIN_IDP_LOGOUT_PATH . '?' . $getParams;
    header('Location: ' . $url, TRUE, 302);
    drupal_exit($url);
  }

  /**
   * {@inheritdoc}
   */
  public function fetchValue($key) {
    $value = NULL;
    if (isset($this->values[$key])) {
      $value = $this->values[$key];
    }

    if ($this->fetchOnly) {
      unset($this->values[$key]);
    }
    return $value;
  }

  /**
   * {@inheritdoc}
   */
  public function isAuthenticated() {
    // If user has any authenticated data consider it as authenticated.
    return !empty($this->values);
  }

  /**
   * {@inheritdoc}
   */
  public function settingsForm() {
    $form = array();
    $form['nemlogin_idp_url'] = array(
      '#type' => 'textfield',
      '#title' => t('URL of IDP system.'),
      '#required' => TRUE,
      '#default_value' => variable_get('nemlogin_idp_url'),
      '#description' => t('E.g. https://nemlogin.bellcom.dk/simplesaml. NB! Do not include the trailing slash.'),
    );
    $form['nemlogin_idp_fetch_only'] = array(
      '#type' => 'checkbox',
      '#title' => t('Use fetch only mode.'),
      '#default_value' => variable_get('nemlogin_idp_fetch_only', NEMLOGIN_IDP_FETCH_ONLY),
      '#description' => t('User will be logged out immediately after login. User data will be removed from session after first retrieving'),
    );

    return $form;
  }

  /**
   * {@inheritdoc}
   */
  public function settingsFormValidate(array &$form, array &$form_state) {
    parent::settingsFormValidate($form, $form_state);

    $idp = $form_state['values']['nemlogin_idp_url'];
    if (strcmp(substr($idp, -1), '/') === 0) {
      form_set_error('nemlogin_idp_url', t('Please remove the trailing slash'));
    }

    $url_to_test = array(
      $idp . NEMLOGIN_IDP_LOGINSERVICE_PATH,
      $idp . NEMLOGIN_IDP_LOGIN_PATH,
      $idp . NEMLOGIN_IDP_LOGOUT_PATH,
    );

    // Testing if we have access to all URLs.
    foreach ($url_to_test as $url) {
      $url_request = drupal_http_request($url);
      if ($url_request->code != 200 && $url_request->redirect_code != 302) {
        form_set_error('nemlogin_idp_url', t('%url cannot be accessed. Response code: %code', array(
          '%url' => $url,
          '%code' => $url_request->code,
        )));
      }
    }
  }

}