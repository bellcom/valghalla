<?php

/**
 * @file
 * The wrapper class to work with Serviceplatformen Print service.
 */

/**
 * Class that holds the service requests.
 */
class ServicePlatformenPrint {

  /**
   * SOAPClient object.
   *
   * @var object
   */
  protected $client;

  /**
   * SOAP Request array.
   *
   * @var array
   */
  protected $request;

  /**
   * System ID.
   *
   * @var string
   */
  protected $systemId;

  /**
   * ServiceplatformenPrint constructor.
   */
  public function __construct() {
    global $user;

    $wsdl_address = variable_get('valghalla_eboks_wsdl');
    $service_location = variable_get('valghalla_eboks_location');
    $certfile_passphrase = variable_get('valghalla_eboks_certfile_passphrase');
    $certfile = variable_get('valghalla_eboks_certfile');

    try {
      $this->client = new SoapClient($wsdl_address, array(
        'location' => $service_location,
        'local_cert' => $certfile,
        'passphrase' => $certfile_passphrase,
        'trace' => TRUE,
      ));
    }
    catch (Exception $e) {
      watchdog('ServicePlatformenPrint', $e->getMessage(), array(), WATCHDOG_ERROR);
    }

    $serviceagreementuuid = variable_get('valghalla_eboks_serviceagreementuuid');
    $usersystemuuid = variable_get('valghalla_eboks_usersystemuuid');
    $useruuid = variable_get('valghalla_eboks_useruuid');
    $serviceuuid = variable_get('valghalla_eboks_serviceuuid');
    $accountinginfo = variable_get('valghalla_eboks_accountinginfo');

    $this->systemId = variable_get('valghalla_eboks_sys_id');

    $this->request = array(
      'InvocationContext' => array(
        'ServiceAgreementUUID' => $serviceagreementuuid,
        'UserSystemUUID' => $usersystemuuid,
        'UserUUID' => $useruuid,
        'ServiceUUID' => $serviceuuid,
        'AccountingInfo' => $accountinginfo,
        'OnBehalfOfUser' => $user->name,
      ),
    );
  }

  /**
   * Get Request data.
   */
  public function getRequest() {
    return $this->request;
  }

  /**
   * Send message method call.
   */
  public function sendMessage() {
    if (empty($this->client)) {
      return array(
        'status' => FALSE,
        'response' => 'WS call failed. Check error log for details.',
      );
    }

    ini_set('soap.wsdl_cache_enabled', 0);
    ini_set('soap.wsdl_cache_ttl', 0);
    $response = array(
      'status' => FALSE,
    );

    $tries_counter = 0;
    do {
      // By default there is only one try to send message.
      $retry = FALSE;

      try {
        $tries_counter++;
        $result = $this->client->afsendBrev($this->request);
        $response['response'] = $this->client->__getLastResponse();
        if (!empty($result->resultat) && $result->resultat) {
          $response['status'] = TRUE;
        }
      }
      catch (Exception $e) {
        $response = array(
          'status' => FALSE,
          'text' => $e->faultstring,
          'code' => $e->faultcode,
          'response' => $this->client->__getLastResponse(),
        );
      }
      if (variable_get('valghalla_eboks_debug', FALSE)) {
        $path = file_directory_temp() . '/service-print-soap';
        if (!file_exists($path)) {
          mkdir($path, 0777, TRUE);
        }
        $path .= '/request-' . time() . '-' . uniqid() . '.xml';
        file_unmanaged_save_data($this->client->__getLastRequest(), $path, FILE_EXISTS_REPLACE);
        watchdog('ServicePlatformenPrint', 'XML-request filesaved to %path', array('%path' => $path));
      }

      $retry_amount = (int) variable_get('valghalla_eboks_retry_max_attempts');
      $retry_allowed_coded = explode("\n", variable_get('valghalla_eboks_retry_allowed_failed_codes'));
      if (!$response['status']
        // Resending happen only for specific fail codes.
        && !empty($e->detail->Fejl->FejlKode)
        // Not more than specified amount of times.
        && $tries_counter < $retry_amount
        // And not more than fixed amount.
        && $tries_counter < VALGHALLA_EBOKS_MAX_FAILED_TRIES
        // Only when we have specified fail codes.
        && !empty($retry_allowed_coded)
        // And gotten fail code we got present in allowed list.
        && in_array($e->detail->Fejl->FejlKode, $retry_allowed_coded)) {
        $retry = TRUE;
      }
    } while ($retry);

    return $response;

  }

  /**
   * Render entire message to send.
   *
   * @param array $params
   *   Array with message sources.
   *
   * @return string
   *   Formatted message.
   */
  public function renderMessage(array $params) {
    $message = format_string($params['body'], $params['tokens']);
    return check_markup($message, isset($params['format']) ? $params['format'] : NULL);
  }

  /**
   * Generate unique ID for message.
   *
   * @param array $params
   *   Message parameters.
   *
   * @return string
   *   Message id.
   */
  protected function getId(array $params) {
    $date = date('Ymd', strtotime('now'));
    $message_id = empty($params['notification_id']) ? strtotime('now') : ($params['notification_id']);
    return sprintf('%06d', $this->systemId) . 'VLB' . $date . sprintf('%013d', $message_id);
  }

}
